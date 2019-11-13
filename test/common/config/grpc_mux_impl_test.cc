#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"

#include "common/common/empty_string.h"
#include "common/config/new_grpc_mux_impl.h"
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
#include "test/test_common/test_time.h"
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
using testing::ReturnRef;

namespace Envoy {
namespace Config {
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of GrpcMuxImpl
// is provided in [grpc_]subscription_impl_test.cc.
class GrpcMuxImplTestBase : public testing::Test {
public:
  GrpcMuxImplTestBase()
      : async_client_(new Grpc::MockAsyncClient()),
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)) {}

  void setup() {
    grpc_mux_ = std::make_unique<GrpcMuxSotw>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, rate_limit_settings_, local_info_, true);
  }

  void setup(const RateLimitSettings& custom_rate_limit_settings) {
    grpc_mux_ = std::make_unique<GrpcMuxSotw>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, custom_rate_limit_settings, local_info_, true);
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names, const std::string& version,
                         bool first = false, const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
                         const std::string& error_message = "") {
    envoy::api::v2::DiscoveryRequest expected_request;
    if (first) {
      expected_request.mutable_node()->CopyFrom(local_info_.node());
    }
    for (const auto& resource : resource_names) {
      expected_request.add_resource_names(resource);
    }
    if (!version.empty()) {
      expected_request.set_version_info(version);
    }
    expected_request.set_response_nonce(nonce);
    expected_request.set_type_url(type_url);
    if (error_code != Grpc::Status::WellKnownGrpcStatus::Ok) {
      ::google::rpc::Status* error_detail = expected_request.mutable_error_detail();
      error_detail->set_code(error_code);
      error_detail->set_message(error_message);
    }
    EXPECT_CALL(
        async_stream_,
        sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(expected_request), false));
  }

  // These tests were written around GrpcMuxWatch, an RAII type returned by the old subscribe().
  // To preserve these tests for the new code, we need an RAII watch handler. That is
  // GrpcSubscriptionImpl, but to keep things simple, we'll fake it. (What we really care about
  // is the destructor, which is identical to the real one).
  class FakeGrpcSubscription {
  public:
    FakeGrpcSubscription(GrpcMux* grpc_mux, std::string type_url, Watch* watch)
        : grpc_mux_(grpc_mux), type_url_(std::move(type_url)), watch_(watch) {}
    ~FakeGrpcSubscription() { grpc_mux_->removeWatch(type_url_, watch_); }

  private:
    GrpcMux* const grpc_mux_;
    std::string type_url_;
    Watch* const watch_;
  };

  FakeGrpcSubscription makeWatch(const std::string& type_url,
                                 const std::set<std::string>& resources) {
    return FakeGrpcSubscription(grpc_mux_.get(), type_url,
                                grpc_mux_->addOrUpdateWatch(type_url, nullptr, resources,
                                                            callbacks_,
                                                            std::chrono::milliseconds(0)));
  }

  FakeGrpcSubscription
  makeWatch(const std::string& type_url, const std::set<std::string>& resources,
            NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>>& callbacks) {
    return FakeGrpcSubscription(grpc_mux_.get(), type_url,
                                grpc_mux_->addOrUpdateWatch(type_url, nullptr, resources, callbacks,
                                                            std::chrono::milliseconds(0)));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<GrpcMuxSotw> grpc_mux_;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Stats::Gauge& control_plane_connected_state_;
};

class GrpcMuxImplTest : public GrpcMuxImplTestBase {
public:
  Event::SimulatedTimeSystem time_system_;
};

// Validate behavior when multiple type URL watches are maintained, watches are created/destroyed.
TEST_F(GrpcMuxImplTest, MultipleTypeUrlStreams) {
  setup();
  InSequence s;

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});
  FakeGrpcSubscription bar_sub = makeWatch("type_url_bar", {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  grpc_mux_->start();
  EXPECT_EQ(1, control_plane_connected_state_.value());
  expectSendMessage("type_url_bar", {"z"}, "");
  FakeGrpcSubscription bar_z_sub = makeWatch("type_url_bar", {"z"});
  expectSendMessage("type_url_bar", {"zz", "z"}, "");
  FakeGrpcSubscription bar_zz_sub = makeWatch("type_url_bar", {"zz"});
  expectSendMessage("type_url_bar", {"z"}, "");
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_foo", {}, "");
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
  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});
  FakeGrpcSubscription bar_sub = makeWatch("type_url_bar", {});
  FakeGrpcSubscription baz_sub = makeWatch("type_url_baz", {"z"});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_baz", {"z"}, "");
  grpc_mux_->start();

  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _))
      .Times(3);
  EXPECT_CALL(random_, random());
  ASSERT_TRUE(timer != nullptr); // initialized from dispatcher mock.
  EXPECT_CALL(*timer, enableTimer(_, _));
  grpc_mux_->grpcStreamForTest().onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_EQ(0, control_plane_connected_state_.value());
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_baz", {"z"}, "");
  timer_cb();

  expectSendMessage("type_url_baz", {}, "");
  expectSendMessage("type_url_foo", {}, "");
}

// Validate pause-resume behavior.
TEST_F(GrpcMuxImplTest, PauseResume) {
  setup();
  InSequence s;
  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});
  grpc_mux_->pause("type_url_foo");
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  grpc_mux_->resume("type_url_foo");
  grpc_mux_->pause("type_url_bar");
  expectSendMessage("type_url_foo", {"z", "x", "y"}, "");
  FakeGrpcSubscription foo_z_sub = makeWatch("type_url_foo", {"z"});
  grpc_mux_->resume("type_url_bar");
  grpc_mux_->pause("type_url_foo");
  FakeGrpcSubscription foo_zz_sub = makeWatch("type_url_foo", {"zz"});
  expectSendMessage("type_url_foo", {"zz", "z", "x", "y"}, "");
  grpc_mux_->resume("type_url_foo");
  grpc_mux_->pause("type_url_foo");
}

// Validate behavior when type URL mismatches occur.
TEST_F(GrpcMuxImplTest, TypeUrlMismatch) {
  setup();

  auto invalid_response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
    response->set_type_url("type_url_bar");
    grpc_mux_->onDiscoveryResponse(std::move(response));
  }

  {
    invalid_response->set_type_url("type_url_foo");
    invalid_response->mutable_resources()->Add()->set_type_url("type_url_bar");
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _))
        .WillOnce(Invoke([](Envoy::Config::ConfigUpdateFailureReason, const EnvoyException* e) {
          EXPECT_TRUE(
              IsSubstring("", "",
                          "type URL type_url_bar embedded in an individual Any does not match the "
                          "message-wide type URL type_url_foo in DiscoveryResponse",
                          e->what()));
        }));

    expectSendMessage(
        "type_url_foo", {"x", "y"}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Internal,
        fmt::format("type URL type_url_bar embedded in an individual Any does not match the "
                    "message-wide type URL type_url_foo in DiscoveryResponse {}",
                    invalid_response->DebugString()));
    grpc_mux_->onDiscoveryResponse(std::move(invalid_response));
  }
  expectSendMessage("type_url_foo", {}, "");
}

// Validate behavior when watches has an unknown resource name.
TEST_F(GrpcMuxImplTest, WildcardWatch) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
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
  grpc_mux_->onDiscoveryResponse(std::move(response));
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_F(GrpcMuxImplTest, WatchDemux) {
  setup();
  // We will not require InSequence here: an update that causes multiple onConfigUpdates
  // causes them in an indeterminate order, based on the whims of the hash map.
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> foo_callbacks;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks);
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> bar_callbacks;
  FakeGrpcSubscription bar_sub = makeWatch(type_url, {"y", "z"}, bar_callbacks);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Should dedupe the "x" resource.
  expectSendMessage(type_url, {"y", "z", "x"}, "", true);
  grpc_mux_->start();

  // Send just x; only foo_callbacks should receive an onConfigUpdate().
  {
    auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "1")).Times(0);
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
    grpc_mux_->onDiscoveryResponse(std::move(response));
  }

  // Send x y and z; foo_ and bar_callbacks should both receive onConfigUpdate()s, carrying {x,y}
  // and {y,z} respectively.
  {
    auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
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
    grpc_mux_->onDiscoveryResponse(std::move(response));
  }

  expectSendMessage(type_url, {"x", "y"}, "2");
  expectSendMessage(type_url, {}, "2");
}

// Validate behavior when we have multiple watchers that send empty updates.
TEST_F(GrpcMuxImplTest, MultipleWatcherWithEmptyUpdates) {
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> foo_callbacks;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x", "y"}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");

  EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1")).Times(0);
  expectSendMessage(type_url, {"x", "y"}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response));

  expectSendMessage(type_url, {}, "1");
}

// Validate behavior when we have Single Watcher that sends Empty updates.
TEST_F(GrpcMuxImplTest, SingleWatcherWithEmptyUpdates) {
  setup();
  const std::string& type_url = Config::TypeUrl::get().Cluster;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> foo_callbacks;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {}, foo_callbacks);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");
  // Validate that onConfigUpdate is called with empty resources.
  EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
      .WillOnce(Invoke([](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                          const std::string&) { EXPECT_TRUE(resources.empty()); }));
  expectSendMessage(type_url, {}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response));
}

// Exactly one test requires a mock time system to provoke behavior that cannot
// easily be achieved with a SimulatedTimeSystem.
class GrpcMuxImplTestWithMockTimeSystem : public GrpcMuxImplTestBase {
public:
  Event::DelegatingTestTimeSystem<MockTimeSystem> mock_time_system_;
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
  EXPECT_CALL(*mock_time_system_, monotonicTime()).Times(0);

  setup();

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(AtLeast(99));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      grpc_mux_->onDiscoveryResponse(std::move(response));
    }
  };

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
  grpc_mux_->start();

  // Exhausts the limit.
  onReceiveMessage(99);

  // API calls go over the limit but we do not see the stat incremented.
  onReceiveMessage(1);
  EXPECT_EQ(0, stats_.counter("control_plane.rate_limit_enforced").value());
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
  EXPECT_CALL(*mock_time_system_, monotonicTime())
      .WillRepeatedly(Return(std::chrono::steady_clock::time_point{}));

  RateLimitSettings custom_rate_limit_settings;
  custom_rate_limit_settings.enabled_ = true;
  setup(custom_rate_limit_settings);

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(AtLeast(99));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      grpc_mux_->onDiscoveryResponse(std::move(response));
    }
  };

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
  grpc_mux_->start();

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(100), _))
      .Times(AtLeast(1));
  onReceiveMessage(110);
  EXPECT_LE(10, stats_.counter("control_plane.rate_limit_enforced").value());
  EXPECT_LE(
      10,
      stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::Accumulate).value());
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

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(AtLeast(260));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
          new envoy::api::v2::DiscoveryResponse());
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      grpc_mux_->onDiscoveryResponse(std::move(response));
    }
  };

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
  grpc_mux_->start();

  // Validate that rate limit is not enforced for 100 requests.
  onReceiveMessage(100);
  EXPECT_EQ(0, stats_.counter("control_plane.rate_limit_enforced").value());

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(500), _))
      .Times(AtLeast(1));
  onReceiveMessage(160);
  EXPECT_LE(10, stats_.counter("control_plane.rate_limit_enforced").value());
  Stats::Gauge& pending_requests =
      stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_LE(10, pending_requests.value());

  // Validate that drain requests call when there are multiple requests in queue.
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  drain_timer_cb();

  // Check that the pending_requests stat is updated with the queue drain.
  EXPECT_EQ(0, pending_requests.value());
}

// Verifies that a message with no resources is accepted.
TEST_F(GrpcMuxImplTest, UnwatchedTypeAcceptsEmptyResources) {
  setup();

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  {
    // subscribe and unsubscribe to simulate a cluster added and removed
    expectSendMessage(type_url, {"y"}, "", true);
    FakeGrpcSubscription temp_sub = makeWatch(type_url, {"y"});
    expectSendMessage(type_url, {}, "");
  }

  // simulate the server sending empty CLA message to notify envoy that the CLA was removed.
  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  // Although the update will change nothing for us, we will "accept" it, and so according
  // to the spec we should ACK it.
  expectSendMessage(type_url, {}, "1", false, "bar");
  grpc_mux_->onDiscoveryResponse(std::move(response));

  // When we become interested in "x", we should send a request indicating that interest.
  expectSendMessage(type_url, {"x"}, "1", false, "bar");
  FakeGrpcSubscription sub = makeWatch(type_url, {"x"});

  // Watch destroyed -> interest gone -> unsubscribe request.
  expectSendMessage(type_url, {}, "1", false, "bar");
}

// Verifies that a message with some resources is accepted even when there are no watches.
// Rationale: SotW gRPC xDS has always been willing to accept updates that include
// uninteresting resources. It should not matter whether those uninteresting resources
// are accompanied by interesting ones.
// Note: this was previously "rejects", not "accepts". See
// https://github.com/envoyproxy/envoy/pull/8350#discussion_r328218220 for discussion.
TEST_F(GrpcMuxImplTest, UnwatchedTypeAcceptsResources) {
  setup();
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  grpc_mux_->start();

  // subscribe and unsubscribe so that the type is known to envoy
  {
    expectSendMessage(type_url, {"y"}, "", true);
    expectSendMessage(type_url, {}, "");
    FakeGrpcSubscription delete_immediately = makeWatch(type_url, {"y"});
  }
  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
  response->set_type_url(type_url);
  envoy::api::v2::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("x");
  response->add_resources()->PackFrom(load_assignment);
  response->set_version_info("1");

  expectSendMessage(type_url, {}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response));
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyClusterName) {
  EXPECT_CALL(local_info_, clusterName()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxSotw(
          std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_, rate_limit_settings_, local_info_, true),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyNodeName) {
  EXPECT_CALL(local_info_, nodeName()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxSotw(
          std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_, rate_limit_settings_, local_info_, true),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

// Test that we simply ignore a message for an unknown type_url, with no ill effects.
TEST_F(GrpcMuxImplTest, DiscoveryResponseNonexistentSub) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  grpc_mux_->addOrUpdateWatch(type_url, nullptr, {}, callbacks_, std::chrono::milliseconds(0));

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();
  {
    auto unexpected_response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
    unexpected_response->set_type_url("unexpected_type_url");
    unexpected_response->set_version_info("0");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "0")).Times(0);
    grpc_mux_->onDiscoveryResponse(std::move(unexpected_response));
  }
  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>();
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
  grpc_mux_->onDiscoveryResponse(std::move(response));
}

} // namespace
} // namespace Config
} // namespace Envoy
