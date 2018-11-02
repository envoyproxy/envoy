#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"

#include "common/config/grpc_mux_impl.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::IsSubstring;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of GrpcMuxImpl
// is provided in [grpc_]subscription_impl_test.cc.
class GrpcMuxImplTest : public testing::Test {
public:
  GrpcMuxImplTest() : async_client_(new Grpc::MockAsyncClient()) {
    dispatcher_.setTimeSystem(time_system_);
  }

  void setup() {
    grpc_mux_ = std::make_unique<GrpcMuxImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, rate_limit_settings_);
  }

  void setup(const RateLimitSettings& custom_rate_limit_settings) {
    grpc_mux_ = std::make_unique<GrpcMuxImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, custom_rate_limit_settings);
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names, const std::string& version,
                         const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::GrpcStatus::Ok,
                         const std::string& error_message = "") {
    envoy::api::v2::DiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(local_info_.node());
    for (const auto& resource : resource_names) {
      expected_request.add_resource_names(resource);
    }
    if (!version.empty()) {
      expected_request.set_version_info(version);
    }
    expected_request.set_response_nonce(nonce);
    expected_request.set_type_url(type_url);
    if (error_code != Grpc::Status::GrpcStatus::Ok) {
      ::google::rpc::Status* error_detail = expected_request.mutable_error_detail();
      error_detail->set_code(error_code);
      error_detail->set_message(error_message);
    }
    EXPECT_CALL(async_stream_, sendMessage(ProtoEq(expected_request), false));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Runtime::MockRandomGenerator random_;
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<GrpcMuxImpl> grpc_mux_;
  NiceMock<MockGrpcMuxCallbacks> callbacks_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
};

// Validate behavior when multiple type URL watches are maintained, watches are created/destroyed
// (via RAII).
TEST_F(GrpcMuxImplTest, MultipleTypeUrlStreams) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  auto bar_sub = grpc_mux_->subscribe("bar", {}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  grpc_mux_->start();
  EXPECT_EQ(1, stats_.gauge("control_plane.connected_state").value());
  expectSendMessage("bar", {"z"}, "");
  auto bar_z_sub = grpc_mux_->subscribe("bar", {"z"}, callbacks_);
  expectSendMessage("bar", {"zz", "z"}, "");
  auto bar_zz_sub = grpc_mux_->subscribe("bar", {"zz"}, callbacks_);
  expectSendMessage("bar", {"z"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate behavior when multiple type URL watches are maintained and the stream is reset.
TEST_F(GrpcMuxImplTest, ResetStream) {
  InSequence s;

  Event::MockTimer* timer = nullptr;
  Event::TimerCb timer_cb;
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([&timer, &timer_cb](Event::TimerCb cb) {
    timer_cb = cb;
    EXPECT_EQ(nullptr, timer);
    timer = new Event::MockTimer();
    return timer;
  }));

  setup();
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  auto bar_sub = grpc_mux_->subscribe("bar", {}, callbacks_);
  auto baz_sub = grpc_mux_->subscribe("baz", {"z"}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  grpc_mux_->start();

  EXPECT_CALL(random_, random());
  ASSERT_TRUE(timer != nullptr); // initialized from dispatcher mock.
  EXPECT_CALL(*timer, enableTimer(_));
  grpc_mux_->onRemoteClose(Grpc::Status::GrpcStatus::Canceled, "");
  EXPECT_EQ(0, stats_.gauge("control_plane.connected_state").value());
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  timer_cb();

  expectSendMessage("baz", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate pause-resume behavior.
TEST_F(GrpcMuxImplTest, PauseResume) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  grpc_mux_->pause("foo");
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();
  expectSendMessage("foo", {"x", "y"}, "");
  grpc_mux_->resume("foo");
  grpc_mux_->pause("bar");
  expectSendMessage("foo", {"z", "x", "y"}, "");
  auto foo_z_sub = grpc_mux_->subscribe("foo", {"z"}, callbacks_);
  grpc_mux_->resume("bar");
  grpc_mux_->pause("foo");
  auto foo_zz_sub = grpc_mux_->subscribe("foo", {"zz"}, callbacks_);
  expectSendMessage("foo", {"zz", "z", "x", "y"}, "");
  grpc_mux_->resume("foo");
  grpc_mux_->pause("foo");
}

// Validate behavior when type URL mismatches occur.
TEST_F(GrpcMuxImplTest, TypeUrlMismatch) {
  setup();

  std::unique_ptr<envoy::api::v2::DiscoveryResponse> invalid_response(
      new envoy::api::v2::DiscoveryResponse());
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);

  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url("bar");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  {
    invalid_response->set_type_url("foo");
    invalid_response->mutable_resources()->Add()->set_type_url("bar");
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_)).WillOnce(Invoke([](const EnvoyException* e) {
      EXPECT_TRUE(
          IsSubstring("", "", "bar does not match foo type URL in DiscoveryResponse", e->what()));
    }));

    expectSendMessage("foo", {"x", "y"}, "", "", Grpc::Status::GrpcStatus::Internal,
                      fmt::format("bar does not match foo type URL in DiscoveryResponse {}",
                                  invalid_response->DebugString()));
    grpc_mux_->onReceiveMessage(std::move(invalid_response));
  }
  expectSendMessage("foo", {}, "");
}

// Validate behavior when watches has an unknown resource name.
TEST_F(GrpcMuxImplTest, WildcardWatch) {
  setup();

  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = grpc_mux_->subscribe(type_url, {}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(
            Invoke([&load_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            }));
    expectSendMessage(type_url, {}, "1");
    grpc_mux_->onReceiveMessage(std::move(response));
  }
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_F(GrpcMuxImplTest, WatchDemux) {
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockGrpcMuxCallbacks> foo_callbacks;
  auto foo_sub = grpc_mux_->subscribe(type_url, {"x", "y"}, foo_callbacks);
  NiceMock<MockGrpcMuxCallbacks> bar_callbacks;
  auto bar_sub = grpc_mux_->subscribe(type_url, {"y", "z"}, bar_callbacks);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  // Should dedupe the "x" resource.
  expectSendMessage(type_url, {"y", "z", "x"}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                            const std::string&) { EXPECT_TRUE(resources.empty()); }));
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
        .WillOnce(
            Invoke([&load_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            }));
    expectSendMessage(type_url, {"y", "z", "x"}, "1");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("2");
    envoy::api::v2::ClusterLoadAssignment load_assignment_x;
    load_assignment_x.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment_x);
    envoy::api::v2::ClusterLoadAssignment load_assignment_y;
    load_assignment_y.set_cluster_name("y");
    response->add_resources()->PackFrom(load_assignment_y);
    envoy::api::v2::ClusterLoadAssignment load_assignment_z;
    load_assignment_z.set_cluster_name("z");
    response->add_resources()->PackFrom(load_assignment_z);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke(
            [&load_assignment_y, &load_assignment_z](
                const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
              EXPECT_EQ(2, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_y));
              resources[1].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_z));
            }));
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke(
            [&load_assignment_x, &load_assignment_y](
                const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
              EXPECT_EQ(2, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_x));
              resources[1].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_y));
            }));
    expectSendMessage(type_url, {"y", "z", "x"}, "2");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  expectSendMessage(type_url, {"x", "y"}, "2");
  expectSendMessage(type_url, {}, "2");
}

// Exactly one test requires a mock time system to provoke behavior that cannot
// easily be achieved with a SimulatedTimeSystem.
class GrpcMuxImplTestWithMockTimeSystem : public GrpcMuxImplTest {
protected:
  GrpcMuxImplTestWithMockTimeSystem() { dispatcher_.setTimeSystem(mock_time_system_); }

  MockTimeSystem mock_time_system_;
};

//  Verifies that rate limiting is not enforced with defaults.
TEST_F(GrpcMuxImplTestWithMockTimeSystem, TooManyRequestsWithDefaultSettings) {
  // Validate that only connection retry timer is enabled.
  Event::MockTimer* timer = nullptr;
  Event::TimerCb timer_cb;
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([&timer, &timer_cb](Event::TimerCb cb) {
    timer_cb = cb;
    EXPECT_EQ(nullptr, timer);
    timer = new Event::MockTimer();
    return timer;
  }));

  // Validate that rate limiter is not created.
  EXPECT_CALL(mock_time_system_, monotonicTime()).Times(0);

  setup();

  EXPECT_CALL(async_stream_, sendMessage(_, false)).Times(AtLeast(99));
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      grpc_mux_->onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->subscribe("foo", {"x"}, callbacks_);
  expectSendMessage("foo", {"x"}, "");
  grpc_mux_->start();

  // Exhausts the limit.
  onReceiveMessage(99);

  // API calls go over the limit but we do not get any message.
  EXPECT_LOG_NOT_CONTAINS("warning", "Too many sendDiscoveryRequest calls", onReceiveMessage(1));
}

//  Verifies that default rate limiting is enforced with empty RateLimitSettings.
TEST_F(GrpcMuxImplTestWithMockTimeSystem, TooManyRequestsWithEmptyRateLimitSettings) {
  // Validate that request drain timer is created.
  Event::MockTimer* timer = nullptr;
  Event::MockTimer* drain_request_timer = nullptr;

  Event::TimerCb timer_cb;
  EXPECT_CALL(dispatcher_, createTimer_(_))
      .WillOnce(Invoke([&timer, &timer_cb](Event::TimerCb cb) {
        timer_cb = cb;
        EXPECT_EQ(nullptr, timer);
        timer = new Event::MockTimer();
        return timer;
      }))
      .WillOnce(Invoke([&drain_request_timer, &timer_cb](Event::TimerCb cb) {
        timer_cb = cb;
        EXPECT_EQ(nullptr, drain_request_timer);
        drain_request_timer = new Event::MockTimer();
        return drain_request_timer;
      }));
  EXPECT_CALL(mock_time_system_, monotonicTime())
      .WillRepeatedly(Return(std::chrono::steady_clock::time_point{}));

  RateLimitSettings custom_rate_limit_settings;
  custom_rate_limit_settings.enabled_ = true;
  setup(custom_rate_limit_settings);

  EXPECT_CALL(async_stream_, sendMessage(_, false)).Times(AtLeast(99));
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      grpc_mux_->onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->subscribe("foo", {"x"}, callbacks_);
  expectSendMessage("foo", {"x"}, "");
  grpc_mux_->start();

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(100)));
  EXPECT_LOG_CONTAINS("warning", "Too many sendDiscoveryRequest calls", onReceiveMessage(99));
}

//  Verifies that rate limiting is enforced with custom RateLimitSettings.
TEST_F(GrpcMuxImplTest, TooManyRequestsWithCustomRateLimitSettings) {
  // Validate that request drain timer is created.
  Event::MockTimer* timer = nullptr;
  Event::MockTimer* drain_request_timer = nullptr;

  Event::TimerCb timer_cb;
  Event::TimerCb drain_timer_cb;

  EXPECT_CALL(dispatcher_, createTimer_(_))
      .WillOnce(Invoke([&timer, &timer_cb](Event::TimerCb cb) {
        timer_cb = cb;
        EXPECT_EQ(nullptr, timer);
        timer = new Event::MockTimer();
        return timer;
      }))
      .WillOnce(Invoke([&drain_request_timer, &drain_timer_cb](Event::TimerCb cb) {
        drain_timer_cb = cb;
        EXPECT_EQ(nullptr, drain_request_timer);
        drain_request_timer = new Event::MockTimer();
        return drain_request_timer;
      }));

  RateLimitSettings custom_rate_limit_settings;
  custom_rate_limit_settings.enabled_ = true;
  custom_rate_limit_settings.max_tokens_ = 250;
  custom_rate_limit_settings.fill_rate_ = 2;
  setup(custom_rate_limit_settings);

  EXPECT_CALL(async_stream_, sendMessage(_, false)).Times(AtLeast(260));
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      grpc_mux_->onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->subscribe("foo", {"x"}, callbacks_);
  expectSendMessage("foo", {"x"}, "");
  grpc_mux_->start();

  // Validate that rate limit log does not appear for 100 requests.
  EXPECT_LOG_NOT_CONTAINS("warning", "Too many sendDiscoveryRequest calls", onReceiveMessage(100));

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(500))).Times(AtLeast(1));
  EXPECT_LOG_CONTAINS("warning", "Too many sendDiscoveryRequest calls", onReceiveMessage(160));

  // Validate that drain requests call when there are multiple requests in queue.
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  drain_timer_cb();
}

//  Verifies that a messsage with no resources is accepted.
TEST_F(GrpcMuxImplTest, UnwatchedTypeAcceptsEmptyResources) {
  setup();

  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  {
    // subscribe and unsubscribe to simulate a cluster added and removed
    expectSendMessage(type_url, {"y"}, "");
    auto temp_sub = grpc_mux_->subscribe(type_url, {"y"}, callbacks_);
    expectSendMessage(type_url, {}, "");
  }

  // simulate the server sending empty CLA message to notify envoy that the CLA was removed.
  std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
      new envoy::api::v2::DiscoveryResponse());
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  // This contains zero resources. No discovery request should be sent.
  grpc_mux_->onReceiveMessage(std::move(response));

  // when we add the new subscription version should be 1 and nonce should be bar
  expectSendMessage(type_url, {"x"}, "1", "bar");

  // simulate a new cluster x is added. add CLA subscription for it.
  auto sub = grpc_mux_->subscribe(type_url, {"x"}, callbacks_);
  expectSendMessage(type_url, {}, "1", "bar");
}

//  Verifies that a messsage with some resources is rejected when there are no watches.
TEST_F(GrpcMuxImplTest, UnwatchedTypeRejectsResources) {
  setup();

  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  // subscribe and unsubscribe (by not keeping the return watch) so that the type is known to envoy
  expectSendMessage(type_url, {"y"}, "");
  expectSendMessage(type_url, {}, "");
  grpc_mux_->subscribe(type_url, {"y"}, callbacks_);

  // simulate the server sending CLA message to notify envoy that the CLA was added,
  // even though envoy doesn't expect it. Envoy should reject this update.
  std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
      new envoy::api::v2::DiscoveryResponse());
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  envoy::api::v2::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("x");
  response->add_resources()->PackFrom(load_assignment);

  // The message should be rejected.
  expectSendMessage(type_url, {}, "", "bar");
  EXPECT_LOG_CONTAINS("warning", "Ignoring unwatched type URL " + type_url,
                      grpc_mux_->onReceiveMessage(std::move(response)));
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyClusterName) {
  EXPECT_CALL(local_info_, clusterName()).WillOnce(Return(""));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxImpl(
          local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_, rate_limit_settings_),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyNodeName) {
  EXPECT_CALL(local_info_, nodeName()).WillOnce(Return(""));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxImpl(
          local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_, rate_limit_settings_),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

} // namespace
} // namespace Config
} // namespace Envoy
