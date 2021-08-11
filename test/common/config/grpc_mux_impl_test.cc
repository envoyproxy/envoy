#include <memory>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/api_version.h"
#include "source/common/config/grpc_mux_impl.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/config/utility.h"
#include "source/common/config/version_converter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/resources.h"
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
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)),
        control_plane_pending_requests_(
            stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::NeverImport))

  {}

  void setup() {
    grpc_mux_ = std::make_unique<GrpcMuxImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
        envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_, true);
  }

  void setup(const RateLimitSettings& custom_rate_limit_settings) {
    grpc_mux_ = std::make_unique<GrpcMuxImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
        envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, custom_rate_limit_settings,
        true);
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names, const std::string& version,
                         bool first = false, const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
                         const std::string& error_message = "") {
    API_NO_BOOST(envoy::service::discovery::v3::DiscoveryRequest) expected_request;
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
    EXPECT_CALL(async_stream_, sendMessageRaw_(Grpc::ProtoBufferEq(expected_request), false));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream async_stream_;
  GrpcMuxImplPtr grpc_mux_;
  NiceMock<MockSubscriptionCallbacks> callbacks_;
  NiceMock<MockOpaqueResourceDecoder> resource_decoder_;
  Stats::TestUtil::TestStore stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Stats::Gauge& control_plane_connected_state_;
  Stats::Gauge& control_plane_pending_requests_;
};

class GrpcMuxImplTest : public GrpcMuxImplTestBase {
public:
  Event::SimulatedTimeSystem time_system_;
};

// Validate behavior when multiple type URL watches are maintained, watches are created/destroyed
// (via RAII).
TEST_F(GrpcMuxImplTest, MultipleTypeUrlStreams) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  auto bar_sub = grpc_mux_->addWatch("bar", {}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  expectSendMessage("bar", {}, "");
  grpc_mux_->start();
  EXPECT_EQ(1, control_plane_connected_state_.value());
  expectSendMessage("bar", {"z"}, "");
  auto bar_z_sub = grpc_mux_->addWatch("bar", {"z"}, callbacks_, resource_decoder_, {});
  expectSendMessage("bar", {"zz", "z"}, "");
  auto bar_zz_sub = grpc_mux_->addWatch("bar", {"zz"}, callbacks_, resource_decoder_, {});
  expectSendMessage("bar", {"z"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate behavior when dynamic context parameters are updated.
TEST_F(GrpcMuxImplTest, DynamicContextParameters) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  auto bar_sub = grpc_mux_->addWatch("bar", {}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  expectSendMessage("bar", {}, "");
  grpc_mux_->start();
  // Unknown type, shouldn't do anything.
  local_info_.context_provider_.update_cb_handler_.runCallbacks("baz");
  // Update to foo type should resend Node.
  expectSendMessage("foo", {"x", "y"}, "", true);
  local_info_.context_provider_.update_cb_handler_.runCallbacks("foo");
  // Update to bar type should resend Node.
  expectSendMessage("bar", {}, "", true);
  local_info_.context_provider_.update_cb_handler_.runCallbacks("bar");
  // Adding a new foo resource to the watch shouldn't send Node.
  expectSendMessage("foo", {"z", "x", "y"}, "");
  auto foo_z_sub = grpc_mux_->addWatch("foo", {"z"}, callbacks_, resource_decoder_, {});
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("foo", {}, "");
}

// Validate behavior when multiple type URL watches are maintained and the stream is reset.
TEST_F(GrpcMuxImplTest, ResetStream) {
  InSequence s;

  auto* timer = new Event::MockTimer(&dispatcher_);
  // TTL timers.
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);

  setup();
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  auto bar_sub = grpc_mux_->addWatch("bar", {}, callbacks_, resource_decoder_, {});
  auto baz_sub = grpc_mux_->addWatch("baz", {"z"}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  grpc_mux_->start();

  // Send another message for foo so that the node is cleared in the cached request.
  // This is to test that the node is set again in the first message below.
  expectSendMessage("foo", {"z", "x", "y"}, "");
  auto foo_z_sub = grpc_mux_->addWatch("foo", {"z"}, callbacks_, resource_decoder_, {});

  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _))
      .Times(4);
  EXPECT_CALL(random_, random());
  EXPECT_CALL(*timer, enableTimer(_, _));
  grpc_mux_->grpcStreamForTest().onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_EQ(0, control_plane_connected_state_.value());
  EXPECT_EQ(0, control_plane_pending_requests_.value());
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"z", "x", "y"}, "", true);
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  expectSendMessage("foo", {"x", "y"}, "");
  timer->invokeCallback();

  expectSendMessage("baz", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate pause-resume behavior.
TEST_F(GrpcMuxImplTest, PauseResume) {
  setup();
  InSequence s;
  GrpcMuxWatchPtr foo_sub;
  GrpcMuxWatchPtr foo_z_sub;
  GrpcMuxWatchPtr foo_zz_sub;
  foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  {
    ScopedResume a = grpc_mux_->pause("foo");
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    grpc_mux_->start();
    expectSendMessage("foo", {"x", "y"}, "", true);
  }
  {
    ScopedResume a = grpc_mux_->pause("bar");
    expectSendMessage("foo", {"z", "x", "y"}, "");
    foo_z_sub = grpc_mux_->addWatch("foo", {"z"}, callbacks_, resource_decoder_, {});
  }
  {
    ScopedResume a = grpc_mux_->pause("foo");
    foo_zz_sub = grpc_mux_->addWatch("foo", {"zz"}, callbacks_, resource_decoder_, {});
    expectSendMessage("foo", {"zz", "z", "x", "y"}, "");
  }
  // When nesting, we only have a single resumption.
  {
    ScopedResume a = grpc_mux_->pause("foo");
    ScopedResume b = grpc_mux_->pause("foo");
    foo_zz_sub = grpc_mux_->addWatch("foo", {"zz"}, callbacks_, resource_decoder_, {});
    expectSendMessage("foo", {"zz", "z", "x", "y"}, "");
  }
  grpc_mux_->pause("foo")->cancel();
}

// Validate behavior when type URL mismatches occur.
TEST_F(GrpcMuxImplTest, TypeUrlMismatch) {
  setup();

  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url("bar");
    response->set_version_info("bar-version");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  {
    invalid_response->set_type_url("foo");
    invalid_response->set_version_info("foo-version");
    invalid_response->mutable_resources()->Add()->set_type_url("bar");
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _))
        .WillOnce(Invoke([](Envoy::Config::ConfigUpdateFailureReason, const EnvoyException* e) {
          EXPECT_TRUE(IsSubstring(
              "", "", "bar does not match the message-wide type URL foo in DiscoveryResponse",
              e->what()));
        }));

    expectSendMessage(
        "foo", {"x", "y"}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Internal,
        fmt::format("bar does not match the message-wide type URL foo in DiscoveryResponse {}",
                    invalid_response->DebugString()));
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(invalid_response));
  }
  expectSendMessage("foo", {}, "");
}

TEST_F(GrpcMuxImplTest, RpcErrorMessageTruncated) {
  setup();
  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  grpc_mux_->start();

  { // Large error message sent back to management server is truncated.
    const std::string very_large_type_url(1 << 20, 'A');
    invalid_response->set_type_url("foo");
    invalid_response->set_version_info("invalid");
    invalid_response->mutable_resources()->Add()->set_type_url(very_large_type_url);
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _))
        .WillOnce(Invoke([&very_large_type_url](Envoy::Config::ConfigUpdateFailureReason,
                                                const EnvoyException* e) {
          EXPECT_TRUE(IsSubstring(
              "", "",
              fmt::format("{} does not match the message-wide type URL foo in DiscoveryResponse",
                          very_large_type_url), // Local error message is not truncated.
              e->what()));
        }));
    expectSendMessage("foo", {"x", "y"}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Internal,
                      fmt::format("{}...(truncated)", std::string(4096, 'A')));
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(invalid_response));
  }
  expectSendMessage("foo", {}, "");
}

envoy::service::discovery::v3::Resource heartbeatResource(std::chrono::milliseconds ttl,
                                                          const std::string& name) {
  envoy::service::discovery::v3::Resource resource;

  resource.mutable_ttl()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(ttl.count()));
  resource.set_name(name);

  return resource;
}

envoy::service::discovery::v3::Resource
resourceWithTtl(std::chrono::milliseconds ttl,
                envoy::config::endpoint::v3::ClusterLoadAssignment& cla) {
  envoy::service::discovery::v3::Resource resource;
  resource.mutable_resource()->PackFrom(cla);
  resource.mutable_ttl()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(ttl.count()));

  resource.set_name(cla.cluster_name());

  return resource;
}
// Validates the behavior when the TTL timer expires.
TEST_F(GrpcMuxImplTest, ResourceTTL) {
  setup();

  time_system_.setSystemTime(std::chrono::seconds(0));

  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;
  auto* ttl_timer = new Event::MockTimer(&dispatcher_);
  auto eds_sub = grpc_mux_->addWatch(type_url, {"x"}, callbacks_, resource_decoder, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x"}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");

    auto wrapped_resource = resourceWithTtl(std::chrono::milliseconds(1000), load_assignment);
    response->add_resources()->PackFrom(wrapped_resource);

    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
        }));
    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1000), _));
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  // Increase the TTL.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    auto wrapped_resource = resourceWithTtl(std::chrono::milliseconds(10000), load_assignment);
    response->add_resources()->PackFrom(wrapped_resource);

    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
        }));
    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(10000), _));
    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  // Refresh the TTL with a heartbeat response.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    auto wrapped_resource = heartbeatResource(std::chrono::milliseconds(10000), "x");
    response->add_resources()->PackFrom(wrapped_resource);

    EXPECT_CALL(*ttl_timer, enabled());

    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  // Remove the TTL.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);

    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
        }));
    EXPECT_CALL(*ttl_timer, disableTimer());
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  // Put the TTL back.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    auto wrapped_resource = resourceWithTtl(std::chrono::milliseconds(10000), load_assignment);
    response->add_resources()->PackFrom(wrapped_resource);

    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
        }));
    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(10000), _));
    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  time_system_.setSystemTime(std::chrono::seconds(11));
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, ""))
      .WillOnce(Invoke([](auto, const auto& removed, auto) {
        EXPECT_EQ(1, removed.size());
        EXPECT_EQ("x", removed.Get(0));
      }));
  // Fire the TTL timer.
  EXPECT_CALL(*ttl_timer, disableTimer());
  ttl_timer->invokeCallback();

  expectSendMessage(type_url, {}, "1");
}

// Checks that the control plane identifier is logged
TEST_F(GrpcMuxImplTest, LogsControlPlaneIndentifier) {
  setup();
  std::string type_url = "foo";
  auto foo_sub = grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    response->mutable_control_plane()->set_identifier("control_plane_ID");

    EXPECT_CALL(callbacks_, onConfigUpdate(_, _));
    expectSendMessage(type_url, {}, "1");
    EXPECT_LOG_CONTAINS("debug", "for foo from control_plane_ID",
                        grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response)));
  }
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("2");
    response->mutable_control_plane()->set_identifier("different_ID");

    EXPECT_CALL(callbacks_, onConfigUpdate(_, _));
    expectSendMessage(type_url, {}, "2");
    EXPECT_LOG_CONTAINS("debug", "for foo from different_ID",
                        grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response)));
  }
}

// Validate behavior when watches has an unknown resource name.
TEST_F(GrpcMuxImplTest, WildcardWatch) {
  setup();

  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  auto foo_sub = grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                            const std::string&) {
          EXPECT_EQ(1, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
        }));
    expectSendMessage(type_url, {}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_F(GrpcMuxImplTest, WatchDemux) {
  setup();
  InSequence s;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = grpc_mux_->addWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder, {});
  NiceMock<MockSubscriptionCallbacks> bar_callbacks;
  auto bar_sub = grpc_mux_->addWatch(type_url, {"y", "z"}, bar_callbacks, resource_decoder, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Should dedupe the "x" resource.
  expectSendMessage(type_url, {"y", "z", "x"}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "1")).Times(0);
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                            const std::string&) {
          EXPECT_EQ(1, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
        }));
    expectSendMessage(type_url, {"y", "z", "x"}, "1");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("2");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_x;
    load_assignment_x.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment_x);
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_y;
    load_assignment_y.set_cluster_name("y");
    response->add_resources()->PackFrom(load_assignment_y);
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_z;
    load_assignment_z.set_cluster_name("z");
    response->add_resources()->PackFrom(load_assignment_z);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke([&load_assignment_y, &load_assignment_z](
                             const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(2, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_y));
          const auto& expected_assignment_1 =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[1].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment_1, load_assignment_z));
        }));
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke([&load_assignment_x, &load_assignment_y](
                             const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(2, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_x));
          const auto& expected_assignment_1 =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[1].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment_1, load_assignment_y));
        }));
    expectSendMessage(type_url, {"y", "z", "x"}, "2");
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
  }

  expectSendMessage(type_url, {"x", "y"}, "2");
  expectSendMessage(type_url, {}, "2");
}

// Validate behavior when we have multiple watchers that send empty updates.
TEST_F(GrpcMuxImplTest, MultipleWatcherWithEmptyUpdates) {
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = grpc_mux_->addWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x", "y"}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");

  EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1")).Times(0);
  expectSendMessage(type_url, {"x", "y"}, "1");
  grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));

  expectSendMessage(type_url, {}, "1");
}

// Validate behavior when we have Single Watcher that sends Empty updates.
TEST_F(GrpcMuxImplTest, SingleWatcherWithEmptyUpdates) {
  setup();
  const std::string& type_url = Config::TypeUrl::get().Cluster;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = grpc_mux_->addWatch(type_url, {}, foo_callbacks, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");
  // Validate that onConfigUpdate is called with empty resources.
  EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
      .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
        EXPECT_TRUE(resources.empty());
      }));
  expectSendMessage(type_url, {}, "1");
  grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
}

// Exactly one test requires a mock time system to provoke behavior that cannot
// easily be achieved with a SimulatedTimeSystem.
class GrpcMuxImplTestWithMockTimeSystem : public GrpcMuxImplTestBase {
public:
  Event::DelegatingTestTimeSystem<MockTimeSystem> mock_time_system_;
};

//  Verifies that rate limiting is not enforced with defaults.
TEST_F(GrpcMuxImplTestWithMockTimeSystem, TooManyRequestsWithDefaultSettings) {

  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  // Retry timer,
  new Event::MockTimer(&dispatcher_);

  // Validate that rate limiter is not created.
  EXPECT_CALL(*mock_time_system_, monotonicTime()).Times(0);

  setup();

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(AtLeast(99));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->addWatch("foo", {"x"}, callbacks_, resource_decoder_, {});
  expectSendMessage("foo", {"x"}, "", true);
  grpc_mux_->start();

  // Exhausts the limit.
  onReceiveMessage(99);

  // API calls go over the limit but we do not see the stat incremented.
  onReceiveMessage(1);
  EXPECT_EQ(0, stats_.counter("control_plane.rate_limit_enforced").value());
}

//  Verifies that default rate limiting is enforced with empty RateLimitSettings.
TEST_F(GrpcMuxImplTest, TooManyRequestsWithEmptyRateLimitSettings) {
  // Validate that request drain timer is created.

  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  Event::MockTimer* drain_request_timer = new Event::MockTimer(&dispatcher_);
  Event::MockTimer* retry_timer = new Event::MockTimer(&dispatcher_);

  RateLimitSettings custom_rate_limit_settings;
  custom_rate_limit_settings.enabled_ = true;
  setup(custom_rate_limit_settings);

  // Attempt to send 99 messages. One of them is rate limited (and we never drain).
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(99);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->addWatch("foo", {"x"}, callbacks_, resource_decoder_, {});
  expectSendMessage("foo", {"x"}, "", true);
  grpc_mux_->start();

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(100), _));
  // The drain timer enable is checked twice, once when we limit, again when the watch is destroyed.
  EXPECT_CALL(*drain_request_timer, enabled()).Times(11);
  onReceiveMessage(110);
  EXPECT_EQ(11, stats_.counter("control_plane.rate_limit_enforced").value());
  EXPECT_EQ(11, control_plane_pending_requests_.value());

  // Validate that when we reset a stream with pending requests, it reverts back to the initial
  // query (i.e. the queue is discarded).
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _));
  EXPECT_CALL(random_, random());
  EXPECT_CALL(*retry_timer, enableTimer(_, _));
  grpc_mux_->grpcStreamForTest().onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_EQ(11, control_plane_pending_requests_.value());
  EXPECT_EQ(0, control_plane_connected_state_.value());
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  time_system_.setMonotonicTime(std::chrono::seconds(30));
  retry_timer->invokeCallback();
  EXPECT_EQ(0, control_plane_pending_requests_.value());
  // One more message on the way out when the watch is destroyed.
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
}

//  Verifies that rate limiting is enforced with custom RateLimitSettings.
TEST_F(GrpcMuxImplTest, TooManyRequestsWithCustomRateLimitSettings) {
  // Validate that request drain timer is created.

  // TTL timer.
  auto ttl_timer = new Event::MockTimer(&dispatcher_);
  Event::MockTimer* drain_request_timer = new Event::MockTimer(&dispatcher_);
  // Retry timer.
  new Event::MockTimer(&dispatcher_);

  RateLimitSettings custom_rate_limit_settings;
  custom_rate_limit_settings.enabled_ = true;
  custom_rate_limit_settings.max_tokens_ = 250;
  custom_rate_limit_settings.fill_rate_ = 2;
  setup(custom_rate_limit_settings);

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false)).Times(AtLeast(260));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const auto onReceiveMessage = [&](uint64_t burst) {
    for (uint64_t i = 0; i < burst; i++) {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_version_info("baz");
      response->set_nonce("bar");
      response->set_type_url("foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
    }
  };

  auto foo_sub = grpc_mux_->addWatch("foo", {"x"}, callbacks_, resource_decoder_, {});
  expectSendMessage("foo", {"x"}, "", true);
  grpc_mux_->start();

  // Validate that rate limit is not enforced for 100 requests.
  onReceiveMessage(100);
  EXPECT_EQ(0, stats_.counter("control_plane.rate_limit_enforced").value());

  // Validate that drain_request_timer is enabled when there are no tokens.
  EXPECT_CALL(*drain_request_timer, enableTimer(std::chrono::milliseconds(500), _));
  EXPECT_CALL(*drain_request_timer, enabled()).Times(11);
  onReceiveMessage(160);
  EXPECT_EQ(11, stats_.counter("control_plane.rate_limit_enforced").value());
  EXPECT_EQ(11, control_plane_pending_requests_.value());

  // Validate that drain requests call when there are multiple requests in queue.
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  drain_request_timer->invokeCallback();

  // Check that the pending_requests stat is updated with the queue drain.
  EXPECT_EQ(0, control_plane_pending_requests_.value());
}

//  Verifies that a message with no resources is accepted.
TEST_F(GrpcMuxImplTest, UnwatchedTypeAcceptsEmptyResources) {
  setup();

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  {
    // subscribe and unsubscribe to simulate a cluster added and removed
    expectSendMessage(type_url, {"y"}, "", true);
    auto temp_sub = grpc_mux_->addWatch(type_url, {"y"}, callbacks_, resource_decoder_, {});
    expectSendMessage(type_url, {}, "");
  }

  // simulate the server sending empty CLA message to notify envoy that the CLA was removed.
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  // TODO(fredlas) the expectation of no discovery request here is against the xDS spec.
  // The upcoming xDS overhaul (part of/followup to PR7293) will fix this.
  //
  // This contains zero resources. No discovery request should be sent.
  grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));

  // when we add the new subscription version should be 1 and nonce should be bar
  expectSendMessage(type_url, {"x"}, "1", false, "bar");

  // simulate a new cluster x is added. add CLA subscription for it.
  auto sub = grpc_mux_->addWatch(type_url, {"x"}, callbacks_, resource_decoder_, {});
  expectSendMessage(type_url, {}, "1", false, "bar");
}

//  Verifies that a message with some resources is rejected when there are no watches.
TEST_F(GrpcMuxImplTest, UnwatchedTypeRejectsResources) {
  setup();

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  // subscribe and unsubscribe (by not keeping the return watch) so that the type is known to envoy
  expectSendMessage(type_url, {"y"}, "", true);
  expectSendMessage(type_url, {}, "");
  grpc_mux_->addWatch(type_url, {"y"}, callbacks_, resource_decoder_, {});

  // simulate the server sending CLA message to notify envoy that the CLA was added,
  // even though envoy doesn't expect it. Envoy should reject this update.
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("x");
  response->add_resources()->PackFrom(load_assignment);

  // The message should be rejected.
  expectSendMessage(type_url, {}, "", false, "bar");
  EXPECT_LOG_CONTAINS("warning", "Ignoring unwatched type URL " + type_url,
                      grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response)));
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyClusterName) {
  EXPECT_CALL(local_info_, clusterName()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxImpl(
          local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
          envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_, true),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyNodeName) {
  EXPECT_CALL(local_info_, nodeName()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxImpl(
          local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
          envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_, true),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

} // namespace
} // namespace Config
} // namespace Envoy
