#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/event/timer.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/config_subscription/grpc/xds_mux/grpc_mux_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/config/v2_link_hacks.h"
#include "test/mocks/common.h"
#include "test/mocks/config/custom_config_validators.h"
#include "test/mocks/config/eds_resources_cache.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
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
namespace XdsMux {
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of GrpcMuxImpl
// is provided in [grpc_]subscription_impl_test.cc.
class GrpcMuxImplTestBase : public testing::TestWithParam<bool> {
public:
  GrpcMuxImplTestBase()
      : async_client_(new Grpc::MockAsyncClient()),
        config_validators_(std::make_unique<NiceMock<MockCustomConfigValidators>>()),
        control_plane_stats_(Utility::generateControlPlaneStats(*stats_.rootScope())),
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)),
        control_plane_pending_requests_(
            stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::NeverImport)) {
    // Once "envoy.restart_features.xds_failover_support" is deprecated, the
    // test should no longer be parameterized.
    scoped_runtime_.mergeValues(
        {{"envoy.restart_features.xds_failover_support", GetParam() ? "true" : "false"}});
  }

  void setup() { setup(rate_limit_settings_); }

  void setup(const RateLimitSettings& custom_rate_limit_settings) {
    GrpcMuxContext grpc_mux_context{
        /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
        /*failover_async_client_=*/nullptr,
        /*dispatcher_=*/dispatcher_,
        /*service_method_=*/
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
        /*local_info_=*/local_info_,
        /*rate_limit_settings_=*/custom_rate_limit_settings,
        /*scope_=*/*stats_.rootScope(),
        /*config_validators_=*/std::move(config_validators_),
        /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
        /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
        /*backoff_strategy_=*/
        std::make_unique<JitteredExponentialBackOffStrategy>(
            SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs,
            random_),
        /*target_xds_authority_=*/"",
        /*eds_resources_cache_=*/std::unique_ptr<MockEdsResourcesCache>(eds_resources_cache_)};
    grpc_mux_ = std::make_unique<XdsMux::GrpcMuxSotw>(grpc_mux_context, true);
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names, const std::string& version,
                         bool first = false, const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
                         const std::string& error_message = "",
                         Grpc::MockAsyncStream* async_stream = nullptr) {
    envoy::service::discovery::v3::DiscoveryRequest expected_request;
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
        async_stream ? *async_stream : async_stream_,
        sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(expected_request), false));
  }

  Config::GrpcMuxWatchPtr makeWatch(const std::string& type_url,
                                    const absl::flat_hash_set<std::string>& resources) {
    return grpc_mux_->addWatch(type_url, resources, callbacks_, resource_decoder_, {});
  }

  Config::GrpcMuxWatchPtr makeWatch(const std::string& type_url,
                                    const absl::flat_hash_set<std::string>& resources,
                                    NiceMock<MockSubscriptionCallbacks>& callbacks,
                                    Config::OpaqueResourceDecoderSharedPtr resource_decoder) {
    return grpc_mux_->addWatch(type_url, resources, callbacks, resource_decoder, {});
  }

  TestScopedRuntime scoped_runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream async_stream_;
  // Used for tests invoking updateMuxSource().
  Grpc::MockAsyncClient* replaced_async_client_;
  Grpc::MockAsyncStream replaced_async_stream_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  CustomConfigValidatorsPtr config_validators_;
  std::unique_ptr<XdsMux::GrpcMuxSotw> grpc_mux_;
  NiceMock<MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_{
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")};
  Stats::TestUtil::TestStore stats_;
  ControlPlaneStats control_plane_stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Stats::Gauge& control_plane_connected_state_;
  Stats::Gauge& control_plane_pending_requests_;
  MockEdsResourcesCache* eds_resources_cache_{nullptr};
};

class GrpcMuxImplTest : public GrpcMuxImplTestBase {
public:
  Event::SimulatedTimeSystem time_system_;
};

INSTANTIATE_TEST_SUITE_P(GrpcMuxImpl, GrpcMuxImplTest, ::testing::Bool());

// Validate behavior when multiple type URL watches are maintained, watches are created/destroyed.
TEST_P(GrpcMuxImplTest, MultipleTypeUrlStreams) {
  setup();
  InSequence s;

  auto foo_sub = makeWatch("type_url_foo", {"x", "y"});
  auto bar_sub = makeWatch("type_url_bar", {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  grpc_mux_->start();
  EXPECT_EQ(1, control_plane_connected_state_.value());
  expectSendMessage("type_url_bar", {"z"}, "");
  auto bar_z_sub = makeWatch("type_url_bar", {"z"});
  expectSendMessage("type_url_bar", {"zz", "z"}, "");
  auto bar_zz_sub = makeWatch("type_url_bar", {"zz"});
  expectSendMessage("type_url_bar", {"z"}, "");
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_foo", {}, "");
}

// Validate behavior when multiple type URL watches are maintained and the stream is reset.
TEST_P(GrpcMuxImplTest, ResetStream) {
  InSequence s;

  auto* timer = new Event::MockTimer(&dispatcher_);
  // TTL timers.
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);

  setup();
  auto foo_sub = makeWatch("type_url_foo", {"x", "y"});
  auto bar_sub = makeWatch("type_url_bar", {});
  auto baz_sub = makeWatch("type_url_baz", {"z"});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_baz", {"z"}, "");
  grpc_mux_->start();

  // Send another message for foo so that the node is cleared in the cached request.
  // This is to test that the the node is set again in the first message below.
  expectSendMessage("type_url_foo", {"z", "x", "y"}, "");
  auto foo_z_sub = makeWatch("type_url_foo", {"z"});

  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _))
      .Times(4);
  EXPECT_CALL(random_, random());
  EXPECT_CALL(*timer, enableTimer(_, _));
  grpc_mux_->grpcStreamForTest().onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_EQ(0, control_plane_connected_state_.value());
  EXPECT_EQ(0, control_plane_pending_requests_.value());
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"z", "x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_baz", {"z"}, "");
  expectSendMessage("type_url_foo", {"x", "y"}, "");
  timer->invokeCallback();

  expectSendMessage("type_url_baz", {}, "");
  expectSendMessage("type_url_foo", {}, "");
}

// Validate pause-resume behavior.
TEST_P(GrpcMuxImplTest, PauseResume) {
  setup();
  InSequence s;
  GrpcMuxWatchPtr foo1;
  GrpcMuxWatchPtr foo2;
  GrpcMuxWatchPtr foo3;
  auto foo = grpc_mux_->addWatch("type_url_foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    grpc_mux_->start();
    expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  }
  {
    ScopedResume a = grpc_mux_->pause("type_url_bar");
    expectSendMessage("type_url_foo", {"z", "x", "y"}, "");
    foo1 = grpc_mux_->addWatch("type_url_foo", {"z"}, callbacks_, resource_decoder_, {});
  }
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    foo2 = grpc_mux_->addWatch("type_url_foo", {"zz"}, callbacks_, resource_decoder_, {});
    expectSendMessage("type_url_foo", {"zz", "z", "x", "y"}, "");
  }
  // When nesting, we only have a single resumption.
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    ScopedResume b = grpc_mux_->pause("type_url_foo");
    foo3 = grpc_mux_->addWatch("type_url_foo", {"zzz"}, callbacks_, resource_decoder_, {});
    expectSendMessage("type_url_foo", {"zzz", "zz", "z", "x", "y"}, "");
  }

  grpc_mux_->pause("type_url_foo")->cancel();
}

// Validate behavior when type URL mismatches occur.
TEST_P(GrpcMuxImplTest, TypeUrlMismatch) {
  setup();

  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  auto foo_sub = makeWatch("type_url_foo", {"x", "y"});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  grpc_mux_->start();

  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url("type_url_bar");
    response->set_version_info("bar-version");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  {
    invalid_response->set_type_url("type_url_foo");
    invalid_response->set_version_info("foo-version");
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
    grpc_mux_->onDiscoveryResponse(std::move(invalid_response), control_plane_stats_);
  }
  expectSendMessage("type_url_foo", {}, "");
}

TEST_P(GrpcMuxImplTest, RpcErrorMessageTruncated) {
  setup();
  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  auto foo_sub = makeWatch("type_url_foo", {"x", "y"});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  grpc_mux_->start();

  { // Large error message sent back to management server is truncated.
    const std::string very_large_type_url(1 << 20, 'A');
    invalid_response->set_type_url("type_url_foo");
    invalid_response->set_version_info("invalid");
    invalid_response->mutable_resources()->Add()->set_type_url(very_large_type_url);
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _))
        .WillOnce(Invoke([&very_large_type_url](Envoy::Config::ConfigUpdateFailureReason,
                                                const EnvoyException* e) {
          EXPECT_TRUE(
              IsSubstring("", "",
                          fmt::format("type URL {} embedded in an individual Any does not match "
                                      "the message-wide type URL type_url_foo in DiscoveryResponse",
                                      very_large_type_url), // Local error message is not truncated.
                          e->what()));
        }));
    expectSendMessage("type_url_foo", {"x", "y"}, "", false, "",
                      Grpc::Status::WellKnownGrpcStatus::Internal,
                      fmt::format("type URL {}...(truncated)", std::string(4087, 'A')));
    grpc_mux_->onDiscoveryResponse(std::move(invalid_response), control_plane_stats_);
  }
  expectSendMessage("type_url_foo", {}, "");
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
envoy::service::discovery::v3::Resource
resourceWithEmptyTtl(envoy::config::endpoint::v3::ClusterLoadAssignment& cla) {
  envoy::service::discovery::v3::Resource resource;
  resource.mutable_resource()->PackFrom(cla);
  resource.set_name(cla.cluster_name());
  return resource;
}
// Validates the behavior when the TTL timer expires.
TEST_P(GrpcMuxImplTest, ResourceTTL) {
  setup();

  time_system_.setSystemTime(std::chrono::seconds(0));

  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;
  auto* ttl_timer = new Event::MockTimer(&dispatcher_);
  auto eds_sub = makeWatch(type_url, {"x"}, callbacks_, resource_decoder);

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

    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(1000), _));
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
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

    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
          return absl::OkStatus();
        }));
    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Refresh the TTL with a heartbeat response.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    auto wrapped_resource = heartbeatResource(std::chrono::milliseconds(10000), "x");
    response->add_resources()->PackFrom(wrapped_resource);

    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_TRUE(resources.empty());
          return absl::OkStatus();
        }));

    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Remove the TTL.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(resourceWithEmptyTtl(load_assignment));

    EXPECT_CALL(*ttl_timer, disableTimer());
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
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

    EXPECT_CALL(*ttl_timer, enabled());
    EXPECT_CALL(*ttl_timer, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
          EXPECT_EQ(1, resources.size());
          return absl::OkStatus();
        }));
    // No update, just a change in TTL.
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  time_system_.setSystemTime(std::chrono::seconds(11));
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, ""))
      .WillOnce(Invoke([](auto, const auto& removed, auto) {
        EXPECT_EQ(1, removed.size());
        EXPECT_EQ("x", removed.Get(0));
        return absl::OkStatus();
      }));
  // Fire the TTL timer.
  EXPECT_CALL(*ttl_timer, disableTimer());
  ttl_timer->invokeCallback();

  expectSendMessage(type_url, {}, "1");
}

// Checks that the control plane identifier is logged
TEST_P(GrpcMuxImplTest, LogsControlPlaneIndentifier) {
  setup();

  std::string type_url = "foo";
  auto foo_sub = makeWatch(type_url, {}, callbacks_, resource_decoder_);
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
TEST_P(GrpcMuxImplTest, WildcardWatch) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = makeWatch(type_url, {}, callbacks_, resource_decoder_);
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
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_P(GrpcMuxImplTest, WatchDemux) {
  setup();
  // We will not require InSequence here: an update that causes multiple onConfigUpdates
  // causes them in an indeterminate order, based on the whims of the hash map.
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder_);
  NiceMock<MockSubscriptionCallbacks> bar_callbacks;
  auto bar_sub = makeWatch(type_url, {"y", "z"}, bar_callbacks, resource_decoder_);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Should dedupe the "x" resource.
  expectSendMessage(type_url, {"y", "z", "x"}, "", true);
  grpc_mux_->start();

  // Send just x; only foo_callbacks should receive an onConfigUpdate().
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
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"y", "z", "x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Send x y and z; foo_ and bar_callbacks should both receive onConfigUpdate()s, carrying {x,y}
  // and {y,z} respectively.
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
          return absl::OkStatus();
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
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"y", "z", "x"}, "2");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  expectSendMessage(type_url, {"x", "y"}, "2");
  expectSendMessage(type_url, {}, "2");
}

// Validate behavior when we have multiple watchers that send empty updates.
TEST_P(GrpcMuxImplTest, MultipleWatcherWithEmptyUpdates) {
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder_);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x", "y"}, "", true);
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");

  EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1")).Times(0);
  expectSendMessage(type_url, {"x", "y"}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);

  expectSendMessage(type_url, {}, "1");
}

// Validate behavior when we have Single Watcher that sends Empty updates.
TEST_P(GrpcMuxImplTest, SingleWatcherWithEmptyUpdates) {
  setup();
  const std::string& type_url = Config::TypeUrl::get().Cluster;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  auto foo_sub = makeWatch(type_url, {}, foo_callbacks, resource_decoder_);

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
        return absl::OkStatus();
      }));
  expectSendMessage(type_url, {}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
}

// Exactly one test requires a mock time system to provoke behavior that cannot
// easily be achieved with a SimulatedTimeSystem.
class GrpcMuxImplTestWithMockTimeSystem : public GrpcMuxImplTestBase {
public:
  Event::DelegatingTestTimeSystem<MockTimeSystem> mock_time_system_;
};

INSTANTIATE_TEST_SUITE_P(GrpcMuxImplTestWithMockTimeSystem, GrpcMuxImplTestWithMockTimeSystem,
                         ::testing::Bool());

//  Verifies that rate limiting is not enforced with defaults.
TEST_P(GrpcMuxImplTestWithMockTimeSystem, TooManyRequestsWithDefaultSettings) {

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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  };

  auto foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
  grpc_mux_->start();

  // Exhausts the limit.
  onReceiveMessage(99);

  // API calls go over the limit but we do not see the stat incremented.
  onReceiveMessage(1);
  EXPECT_EQ(0, stats_.counter("control_plane.rate_limit_enforced").value());
}

//  Verifies that default rate limiting is enforced with empty RateLimitSettings.
TEST_P(GrpcMuxImplTest, TooManyRequestsWithEmptyRateLimitSettings) {
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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  };

  auto foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
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
TEST_P(GrpcMuxImplTest, TooManyRequestsWithCustomRateLimitSettings) {
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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  };

  auto foo_sub = makeWatch("type_url_foo", {"x"});
  expectSendMessage("type_url_foo", {"x"}, "", true);
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

// Verifies that a message with no resources is accepted.
TEST_P(GrpcMuxImplTest, UnwatchedTypeAcceptsEmptyResources) {
  setup();

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  grpc_mux_->start();
  {
    // subscribe and unsubscribe to simulate a cluster added and removed
    expectSendMessage(type_url, {"y"}, "", true);
    auto temp_sub = makeWatch(type_url, {"y"});
    expectSendMessage(type_url, {}, "");
  }

  // simulate the server sending empty CLA message to notify envoy that the CLA was removed.
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_nonce("bar");
  response->set_version_info("1");
  response->set_type_url(type_url);

  // Although the update will change nothing for us, we will "accept" it, and so according
  // to the spec we should ACK it.
  expectSendMessage(type_url, {}, "1", false, "bar");
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);

  // When we become interested in "x", we should send a request indicating that interest.
  expectSendMessage(type_url, {"x"}, "1", false, "bar");
  auto sub = makeWatch(type_url, {"x"});

  // Watch destroyed -> interest gone -> unsubscribe request.
  expectSendMessage(type_url, {}, "1", false, "bar");
}

// Verifies that a message with some resources is accepted even when there are no watches.
// Rationale: SotW gRPC xDS has always been willing to accept updates that include
// uninteresting resources. It should not matter whether those uninteresting resources
// are accompanied by interesting ones.
// Note: this was previously "rejects", not "accepts". See
// https://github.com/envoyproxy/envoy/pull/8350#discussion_r328218220 for discussion.
TEST_P(GrpcMuxImplTest, UnwatchedTypeAcceptsResources) {
  setup();
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  grpc_mux_->start();

  // subscribe and unsubscribe so that the type is known to envoy
  {
    expectSendMessage(type_url, {"y"}, "", true);
    expectSendMessage(type_url, {}, "");
    auto delete_immediately = makeWatch(type_url, {"y"});
  }
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_type_url(type_url);
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("x");
  response->add_resources()->PackFrom(load_assignment);
  response->set_version_info("1");

  expectSendMessage(type_url, {}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
}

TEST_P(GrpcMuxImplTest, BadLocalInfoEmptyClusterName) {
  EXPECT_CALL(local_info_, clusterName()).WillOnce(ReturnRef(EMPTY_STRING));
  GrpcMuxContext grpc_mux_context{
      /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
      /*failover_async_client_=*/nullptr,
      /*dispatcher_=*/dispatcher_,
      /*service_method_=*/
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
      /*local_info_=*/local_info_,
      /*rate_limit_settings_=*/rate_limit_settings_,
      /*scope_=*/*stats_.rootScope(),
      /*config_validators_=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
      /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
      /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
      /*backoff_strategy_=*/
      std::make_unique<JitteredExponentialBackOffStrategy>(
          SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_),
      /*target_xds_authority_=*/"",
      /*eds_resources_cache_=*/nullptr};
  EXPECT_THROW_WITH_MESSAGE(
      XdsMux::GrpcMuxSotw(grpc_mux_context, true), EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

TEST_P(GrpcMuxImplTest, BadLocalInfoEmptyNodeName) {
  EXPECT_CALL(local_info_, nodeName()).WillOnce(ReturnRef(EMPTY_STRING));
  GrpcMuxContext grpc_mux_context{
      /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
      /*failover_async_client_=*/nullptr,
      /*dispatcher_=*/dispatcher_,
      /*service_method_=*/
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
      /*local_info_=*/local_info_,
      /*rate_limit_settings_=*/rate_limit_settings_,
      /*scope_=*/*stats_.rootScope(),
      /*config_validators_=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
      /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
      /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
      /*backoff_strategy_=*/
      std::make_unique<JitteredExponentialBackOffStrategy>(
          SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_),
      /*target_xds_authority_=*/"",
      /*eds_resources_cache_=*/nullptr};
  EXPECT_THROW_WITH_MESSAGE(
      XdsMux::GrpcMuxSotw(grpc_mux_context, true), EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

// Validate that a valid resource decoder is used after removing a subscription.
TEST_P(GrpcMuxImplTest, ValidResourceDecoderAfterRemoval) {
  setup();
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  {
    // Subscribe to resource "x" with some callbacks and resource decoder.
    NiceMock<MockSubscriptionCallbacks> foo_callbacks;
    OpaqueResourceDecoderSharedPtr foo_decoder(
        std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
            envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
    auto foo_sub = makeWatch(type_url, {"x"}, foo_callbacks, foo_decoder);

    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    expectSendMessage(type_url, {"x"}, "", true);
    grpc_mux_->start();

    // Send just x; only foo_callbacks should receive an onConfigUpdate(),
    // and foo_decoder should be invoked.
    {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_type_url(type_url);
      response->set_version_info("1");
      envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
      load_assignment.set_cluster_name("x");
      response->add_resources()->PackFrom(load_assignment);
      EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
          .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                              const std::string&) {
            EXPECT_EQ(1, resources.size());
            const auto& expected_assignment =
                dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                    resources[0].get().resource());
            EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            return absl::OkStatus();
          }));
      expectSendMessage(type_url, {"x"}, "1");
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }

    expectSendMessage(type_url, {}, "1");
  }
  // foo_sub no longer valid, watcher was removed, and foo_decoder no longer valid.

  // Subscribe to resource "y" with other callbacks and resource decoder.
  NiceMock<MockSubscriptionCallbacks> bar_callbacks;
  OpaqueResourceDecoderSharedPtr bar_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
  expectSendMessage(type_url, {"y"}, "1");
  auto bar_sub = makeWatch(type_url, {"y"}, bar_callbacks, bar_decoder);

  // Send y; only bar_callbacks should receive an onConfigUpdate(), and
  // bar_decoder should be invoked (not foo_callbacks or foo_decoder).
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("2");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("y");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                            const std::string&) {
          EXPECT_EQ(1, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"y"}, "2");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  expectSendMessage(type_url, {}, "2");
}

// Validate behavior when dynamic context parameters are updated.
TEST_P(GrpcMuxImplTest, DynamicContextParameters) {
  setup();
  InSequence s;
  auto foo = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  auto bar = grpc_mux_->addWatch("bar", {}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "", true);
  expectSendMessage("bar", {}, "");
  grpc_mux_->start();
  // Unknown type, shouldn't do anything.
  ASSERT_TRUE(local_info_.context_provider_.update_cb_handler_.runCallbacks("baz").ok());
  // Update to foo type should resend Node.
  expectSendMessage("foo", {"x", "y"}, "", true);
  ASSERT_TRUE(local_info_.context_provider_.update_cb_handler_.runCallbacks("foo").ok());
  // Update to bar type should resend Node.
  expectSendMessage("bar", {}, "", true);
  ASSERT_TRUE(local_info_.context_provider_.update_cb_handler_.runCallbacks("bar").ok());
  // only destruction of foo watch is going to result in an unsubscribe message.
  // bar watch is empty and its destruction doesn't change it resource list.
  expectSendMessage("foo", {}, "", false);
}

TEST_P(GrpcMuxImplTest, AllMuxesStateTest) {
  setup();
  GrpcMuxContext grpc_mux_context{
      /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(),
      /*failover_async_client_=*/nullptr,
      /*dispatcher_=*/dispatcher_,
      /*service_method_=*/
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
      /*local_info_=*/local_info_,
      /*rate_limit_settings_=*/rate_limit_settings_,
      /*scope_=*/*stats_.rootScope(),
      /*config_validators_=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
      /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
      /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
      /*backoff_strategy_=*/
      std::make_unique<JitteredExponentialBackOffStrategy>(
          SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_),
      /*target_xds_authority_=*/"",
      /*eds_resources_cache_=*/nullptr};
  auto grpc_mux_1 = std::make_unique<XdsMux::GrpcMuxSotw>(grpc_mux_context, true);
  Config::XdsMux::GrpcMuxSotw::shutdownAll();

  EXPECT_TRUE(grpc_mux_->isShutdown());
  EXPECT_TRUE(grpc_mux_1->isShutdown());
}

// Validates that the EDS cache getter returns the cache.
TEST_P(GrpcMuxImplTest, EdsResourcesCacheForEds) {
  eds_resources_cache_ = new NiceMock<MockEdsResourcesCache>();
  setup();
  EXPECT_TRUE(grpc_mux_->edsResourcesCache().has_value());
}

// Validates that the EDS cache getter returns empty if there is no cache.
TEST_P(GrpcMuxImplTest, EdsResourcesCacheForEdsNoCache) {
  setup();
  EXPECT_FALSE(grpc_mux_->edsResourcesCache().has_value());
}

// Validate that an EDS resource is cached if there's a cache.
TEST_P(GrpcMuxImplTest, CacheEdsResource) {
  // Create the cache that will also be passed to the GrpcMux object via setup().
  eds_resources_cache_ = new NiceMock<MockEdsResourcesCache>();
  setup();

  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;
  auto eds_sub = makeWatch(type_url, {"x"});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x"}, "", true);
  grpc_mux_->start();

  // Reply with the resource, it will be added to the cache.
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
          return absl::OkStatus();
        }));
    EXPECT_CALL(*eds_resources_cache_, setResource("x", ProtoEq(load_assignment)));
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Envoy will unsubscribe from all resources.
  EXPECT_CALL(*eds_resources_cache_, removeResource("x"));
  expectSendMessage(type_url, {}, "1");
}

// Validate that an update to an EDS resource watcher is reflected in the cache,
// if there's a cache.
TEST_P(GrpcMuxImplTest, UpdateCacheEdsResource) {
  // Create the cache that will also be passed to the GrpcMux object via setup().
  eds_resources_cache_ = new NiceMock<MockEdsResourcesCache>();
  setup();

  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;
  auto eds_sub = makeWatch(type_url, {"x"});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x"}, "", true);
  grpc_mux_->start();

  // Reply with the resource, it will be added to the cache.
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
          return absl::OkStatus();
        }));
    EXPECT_CALL(*eds_resources_cache_, setResource("x", ProtoEq(load_assignment)));
    expectSendMessage(type_url, {"x"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Update the cache to another resource.
  EXPECT_CALL(*eds_resources_cache_, removeResource("x"));
  expectSendMessage(type_url, {"y"}, "1");
  eds_sub->update({"y"});

  // Envoy will unsubscribe from all resources.
  EXPECT_CALL(*eds_resources_cache_, removeResource("y"));
  expectSendMessage(type_url, {}, "1");
}

// Validate that adding and removing watchers reflects on the cache changes,
// if there's a cache.
TEST_P(GrpcMuxImplTest, AddRemoveSubscriptions) {
  // Create the cache that will also be passed to the GrpcMux object via setup().
  eds_resources_cache_ = new NiceMock<MockEdsResourcesCache>();
  setup();

  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;

  {
    auto eds_sub = makeWatch(type_url, {"x"});

    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    expectSendMessage(type_url, {"x"}, "", true);
    grpc_mux_->start();

    // Reply with the resource, it will be added to the cache.
    {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_type_url(type_url);
      response->set_version_info("1");
      envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
      load_assignment.set_cluster_name("x");
      response->add_resources()->PackFrom(load_assignment);

      EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
          .WillOnce(
              Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
                EXPECT_EQ(1, resources.size());
                return absl::OkStatus();
              }));
      EXPECT_CALL(*eds_resources_cache_, setResource("x", ProtoEq(load_assignment)));
      expectSendMessage(type_url, {"x"}, "1"); // Ack.
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }

    // Watcher (eds_sub) going out of scope, the resource should be removed, as well as
    // the interest.
    EXPECT_CALL(*eds_resources_cache_, removeResource("x"));
    expectSendMessage(type_url, {}, "1");
  }

  // Update to a new resource interest.
  {
    expectSendMessage(type_url, {"y"}, "1");
    auto eds_sub2 = makeWatch(type_url, {"y"});

    // Reply with the resource, it will be added to the cache.
    {
      auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
      response->set_type_url(type_url);
      response->set_version_info("2");
      envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
      load_assignment.set_cluster_name("y");
      response->add_resources()->PackFrom(load_assignment);

      EXPECT_CALL(callbacks_, onConfigUpdate(_, "2"))
          .WillOnce(
              Invoke([](const std::vector<DecodedResourceRef>& resources, const std::string&) {
                EXPECT_EQ(1, resources.size());
                return absl::OkStatus();
              }));
      EXPECT_CALL(*eds_resources_cache_, setResource("y", ProtoEq(load_assignment)));
      expectSendMessage(type_url, {"y"}, "2"); // Ack.
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }

    // Watcher (eds_sub2) going out of scope, the resource should be removed, as well as
    // the interest.
    EXPECT_CALL(*eds_resources_cache_, removeResource("y"));
    expectSendMessage(type_url, {}, "2");
  }
}

// Updating the mux object while being connected sends the correct requests.
TEST_P(GrpcMuxImplTest, MuxDynamicReplacementWhenConnected) {
  replaced_async_client_ = new Grpc::MockAsyncClient();
  setup();
  InSequence s;

  auto foo_sub = makeWatch("type_url_foo", {"x", "y"});
  auto bar_sub = makeWatch("type_url_bar", {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  grpc_mux_->start();
  EXPECT_EQ(1, control_plane_connected_state_.value());

  // Switch the mux.
  envoy::config::core::v3::ApiConfigSource empty_ads_config;
  // Expect a disconnect from the original async_client and stream.
  EXPECT_CALL(async_stream_, resetStream());
  // Expect establishing connection to the new client and stream.
  EXPECT_CALL(*replaced_async_client_, startRaw(_, _, _, _))
      .WillOnce(Return(&replaced_async_stream_));
  // Expect the initial messages to be sent to the new stream.
  expectSendMessage("type_url_foo", {"x", "y"}, "", true, "", Grpc::Status::WellKnownGrpcStatus::Ok,
                    "", &replaced_async_stream_);
  expectSendMessage("type_url_bar", {}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    &replaced_async_stream_);
  EXPECT_OK(grpc_mux_->updateMuxSource(
      /*primary_async_client=*/std::unique_ptr<Grpc::MockAsyncClient>(replaced_async_client_),
      /*failover_async_client=*/nullptr,
      /*custom_config_validators=*/nullptr,
      /*scope=*/*stats_.rootScope(),
      /*backoff_strategy=*/
      std::make_unique<JitteredExponentialBackOffStrategy>(
          SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_),
      empty_ads_config));
  // Ending test, removing subscriptions for type_url_foo.
  expectSendMessage("type_url_foo", {}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    &replaced_async_stream_);
}

// Updating the mux object after receiving a response, sends the correct requests.
TEST_P(GrpcMuxImplTest, MuxDynamicReplacementFetchingResources) {
  replaced_async_client_ = new Grpc::MockAsyncClient();
  setup();
  InSequence s;

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = makeWatch(type_url, {"x", "y"});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x", "y"}, "", true);
  grpc_mux_->start();

  // Send back a response for one of the resources.
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
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"x", "y"}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Switch the mux.
  envoy::config::core::v3::ApiConfigSource empty_ads_config;
  // Expect a disconnect from the original async_client and stream.
  EXPECT_CALL(async_stream_, resetStream());
  // Expect establishing connection to the new client and stream.
  EXPECT_CALL(*replaced_async_client_, startRaw(_, _, _, _))
      .WillOnce(Return(&replaced_async_stream_));
  // Expect the initial message to be sent to the new stream.
  expectSendMessage(type_url, {"x", "y"}, "1", true, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    &replaced_async_stream_);
  EXPECT_OK(grpc_mux_->updateMuxSource(
      /*primary_async_client=*/std::unique_ptr<Grpc::MockAsyncClient>(replaced_async_client_),
      /*failover_async_client=*/nullptr,
      /*custom_config_validators=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
      /*scope=*/*stats_.rootScope(),
      /*backoff_strategy=*/
      std::make_unique<JitteredExponentialBackOffStrategy>(
          SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_),
      empty_ads_config));

  // Send a response to resource "y" on the replaced mux.
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_version_info("2");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("y");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "2"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                            const std::string&) {
          EXPECT_EQ(1, resources.size());
          const auto& expected_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resources[0].get().resource());
          EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
          return absl::OkStatus();
        }));
    expectSendMessage(type_url, {"x", "y"}, "2", false, "", Grpc::Status::WellKnownGrpcStatus::Ok,
                      "", &replaced_async_stream_);
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  // Ending test, removing subscriptions for the subscription.
  expectSendMessage(type_url, {}, "2", false, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    &replaced_async_stream_);
}

// Updating the mux object with wrong rate limit settings is rejected.
TEST_P(GrpcMuxImplTest, RejectMuxDynamicReplacementRateLimitSettingsError) {
  replaced_async_client_ = new Grpc::MockAsyncClient();
  setup();
  InSequence s;

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = makeWatch(type_url, {"x", "y"});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {"x", "y"}, "", true);
  grpc_mux_->start();
  EXPECT_EQ(1, control_plane_connected_state_.value());

  // Switch the mux.
  envoy::config::core::v3::ApiConfigSource ads_config_wrong_settings;
  envoy::config::core::v3::RateLimitSettings* rate_limits =
      ads_config_wrong_settings.mutable_rate_limit_settings();
  rate_limits->mutable_max_tokens()->set_value(500);
  rate_limits->mutable_fill_rate()->set_value(std::numeric_limits<double>::quiet_NaN());
  // No disconnect and replacement of the original async_client.
  EXPECT_CALL(async_stream_, resetStream()).Times(0);
  EXPECT_CALL(*replaced_async_client_, startRaw(_, _, _, _)).Times(0);
  EXPECT_FALSE(grpc_mux_
                   ->updateMuxSource(
                       /*primary_async_client=*/std::unique_ptr<Grpc::MockAsyncClient>(
                           replaced_async_client_),
                       /*failover_async_client=*/nullptr,
                       /*custom_config_validators=*/nullptr,
                       /*scope=*/*stats_.rootScope(),
                       /*backoff_strategy=*/
                       std::make_unique<JitteredExponentialBackOffStrategy>(
                           SubscriptionFactory::RetryInitialDelayMs,
                           SubscriptionFactory::RetryMaxDelayMs, random_),
                       ads_config_wrong_settings)
                   .ok());
  // Ending test, removing subscriptions for type_url_foo.
  expectSendMessage(type_url, {}, "", false, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    &async_stream_);
}

class NullGrpcMuxImplTest : public testing::Test {
public:
  NullGrpcMuxImplTest() : null_mux_(std::make_unique<Config::XdsMux::NullGrpcMuxImpl>()) {}
  Config::GrpcMuxPtr null_mux_;
  NiceMock<MockSubscriptionCallbacks> callbacks_;
};

TEST_F(NullGrpcMuxImplTest, StartImplemented) { EXPECT_NO_THROW(null_mux_->start()); }

TEST_F(NullGrpcMuxImplTest, PauseImplemented) {
  ScopedResume scoped;
  EXPECT_NO_THROW(scoped = null_mux_->pause("ignored"));
}

TEST_F(NullGrpcMuxImplTest, PauseMultipleArgsImplemented) {
  ScopedResume scoped;
  const std::vector<std::string> params = {"ignored", "another_ignored"};
  EXPECT_NO_THROW(scoped = null_mux_->pause(params));
}

TEST_F(NullGrpcMuxImplTest, RequestOnDemandNotImplemented) {
  EXPECT_ENVOY_BUG(null_mux_->requestOnDemandUpdate("type_url", {"for_update"}),
                   "unexpected request for on demand update");
}

TEST_F(NullGrpcMuxImplTest, AddWatchRaisesException) {
  NiceMock<MockSubscriptionCallbacks> callbacks;
  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name"));

  EXPECT_THROW_WITH_REGEX(null_mux_->addWatch("type_url", {}, callbacks, resource_decoder, {}),
                          EnvoyException, "ADS must be configured to support an ADS config source");
}

TEST_F(NullGrpcMuxImplTest, NoEdsResourcesCache) { EXPECT_EQ({}, null_mux_->edsResourcesCache()); }

TEST(UnifiedSotwGrpcMuxFactoryTest, InvalidRateLimit) {
  auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(
      "envoy.config_mux.sotw_grpc_mux_factory");
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  envoy::config::core::v3::ApiConfigSource ads_config;
  ads_config.mutable_rate_limit_settings()->mutable_max_tokens()->set_value(100);
  ads_config.mutable_rate_limit_settings()->mutable_fill_rate()->set_value(
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_THROW(factory->create(std::make_unique<Grpc::MockAsyncClient>(), nullptr, dispatcher,
                               random, scope, ads_config, local_info, nullptr, nullptr,
                               absl::nullopt, absl::nullopt, false),
               EnvoyException);
}

TEST(UnifiedDeltaGrpcMuxFactoryTest, InvalidRateLimit) {
  auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(
      "envoy.config_mux.delta_grpc_mux_factory");
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  envoy::config::core::v3::ApiConfigSource ads_config;
  ads_config.mutable_rate_limit_settings()->mutable_max_tokens()->set_value(100);
  ads_config.mutable_rate_limit_settings()->mutable_fill_rate()->set_value(
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_THROW(factory->create(std::make_unique<Grpc::MockAsyncClient>(), nullptr, dispatcher,
                               random, scope, ads_config, local_info, nullptr, nullptr,
                               absl::nullopt, absl::nullopt, false),
               EnvoyException);
}

} // namespace
} // namespace XdsMux
} // namespace Config
} // namespace Envoy
