#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/empty_string.h"
#include "common/config/api_version.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/isolated_store_impl.h"

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
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of GrpcMuxImpl
// is provided in [grpc_]subscription_impl_test.cc.
class GrpcMuxImplTestBase : public testing::Test {
public:
  GrpcMuxImplTestBase()
      : async_client_(new Grpc::MockAsyncClient()),
        control_plane_stats_(Utility::generateControlPlaneStats(stats_)),
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)),
        control_plane_pending_requests_(
            stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::NeverImport)),
        resource_decoder_(TestUtility::TestOpaqueResourceDecoderImpl<
                          envoy::config::endpoint::v3::ClusterLoadAssignment>("cluster_name")) {}

  void setup() {
    grpc_mux_ = std::make_unique<GrpcMuxSotw>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_,
        local_info_, true);
  }

  void setup(const RateLimitSettings& custom_rate_limit_settings) {
    grpc_mux_ = std::make_unique<GrpcMuxSotw>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, custom_rate_limit_settings,
        local_info_, true);
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names, const std::string& version,
                         bool first = false, const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
                         const std::string& error_message = "") {
    API_NO_BOOST(envoy::api::v2::DiscoveryRequest) expected_request;
    if (first) {
      expected_request.mutable_node()->CopyFrom(API_DOWNGRADE(local_info_.node()));
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
                                grpc_mux_->addWatch(type_url, resources, callbacks_,
                                                    resource_decoder_,
                                                    std::chrono::milliseconds(0)));
  }

  FakeGrpcSubscription makeWatch(const std::string& type_url,
                                 const std::set<std::string>& resources,
                                 NiceMock<MockSubscriptionCallbacks>& callbacks,
                                 Config::OpaqueResourceDecoder& resource_decoder) {
    return FakeGrpcSubscription(grpc_mux_.get(), type_url,
                                grpc_mux_->addWatch(type_url, resources, callbacks,
                                                    resource_decoder,
                                                    std::chrono::milliseconds(0)));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<GrpcMuxSotw> grpc_mux_;
  NiceMock<MockSubscriptionCallbacks> callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::TestUtil::TestStore stats_;
  ControlPlaneStats control_plane_stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Stats::Gauge& control_plane_connected_state_;
  Stats::Gauge& control_plane_pending_requests_;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder_;
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

  auto* timer = new Event::MockTimer(&dispatcher_);
  // TTL timers.
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);
  new Event::MockTimer(&dispatcher_);

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
  EXPECT_CALL(*timer, enableTimer(_, _));
  grpc_mux_->grpcStreamForTest().onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_EQ(0, control_plane_connected_state_.value());
  EXPECT_EQ(0, control_plane_pending_requests_.value());
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  expectSendMessage("type_url_bar", {}, "");
  expectSendMessage("type_url_baz", {"z"}, "");
  timer->invokeCallback();

  expectSendMessage("type_url_baz", {}, "");
  expectSendMessage("type_url_foo", {}, "");
}

// Validate pause-resume behavior.
TEST_F(GrpcMuxImplTest, PauseResume) {
  setup();
  InSequence s;
  grpc_mux_->addWatch("type_url_foo", {"x", "y"}, callbacks_, resource_decoder_,
                      std::chrono::milliseconds(0));
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    grpc_mux_->start();
    expectSendMessage("type_url_foo", {"x", "y"}, "", true);
  }
  {
    ScopedResume a = grpc_mux_->pause("type_url_bar");
    expectSendMessage("type_url_foo", {"z", "x", "y"}, "");
    grpc_mux_->addWatch("type_url_foo", {"z"}, callbacks_, resource_decoder_,
                        std::chrono::milliseconds(0));
  }
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    grpc_mux_->addWatch("type_url_foo", {"zz"}, callbacks_, resource_decoder_,
                        std::chrono::milliseconds(0));
    expectSendMessage("type_url_foo", {"zz", "z", "x", "y"}, "");
  }
  // When nesting, we only have a single resumption.
  {
    ScopedResume a = grpc_mux_->pause("type_url_foo");
    ScopedResume b = grpc_mux_->pause("type_url_foo");
    grpc_mux_->addWatch("type_url_foo", {"zzz"}, callbacks_, resource_decoder_,
                        std::chrono::milliseconds(0));
    expectSendMessage("type_url_foo", {"zzz", "zz", "z", "x", "y"}, "");
  }
  grpc_mux_->pause("type_url_foo")->cancel();
}

// Validate behavior when type URL mismatches occur.
TEST_F(GrpcMuxImplTest, TypeUrlMismatch) {
  setup();

  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});

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

TEST_F(GrpcMuxImplTest, RpcErrorMessageTruncated) {
  setup();
  auto invalid_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  InSequence s;
  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x", "y"});

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
TEST_F(GrpcMuxImplTest, ResourceTTL) {
  setup();

  time_system_.setSystemTime(std::chrono::seconds(0));

  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder("cluster_name");
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  InSequence s;
  auto* ttl_timer = new Event::MockTimer(&dispatcher_);
  FakeGrpcSubscription eds_sub = makeWatch(type_url, {"x"}, callbacks_, resource_decoder);

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
      }));
  // Fire the TTL timer.
  EXPECT_CALL(*ttl_timer, disableTimer());
  ttl_timer->invokeCallback();

  expectSendMessage(type_url, {}, "1");
}

// Validate behavior when watches has an unknown resource name.
TEST_F(GrpcMuxImplTest, WildcardWatch) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {}, callbacks_, resource_decoder_);
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
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_F(GrpcMuxImplTest, WatchDemux) {
  setup();
  // We will not require InSequence here: an update that causes multiple onConfigUpdates
  // causes them in an indeterminate order, based on the whims of the hash map.
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;

  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder_);
  NiceMock<MockSubscriptionCallbacks> bar_callbacks;
  FakeGrpcSubscription bar_sub = makeWatch(type_url, {"y", "z"}, bar_callbacks, resource_decoder_);
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
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
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
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {"x", "y"}, foo_callbacks, resource_decoder_);

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
TEST_F(GrpcMuxImplTest, SingleWatcherWithEmptyUpdates) {
  setup();
  const std::string& type_url = Config::TypeUrl::get().Cluster;
  NiceMock<MockSubscriptionCallbacks> foo_callbacks;
  FakeGrpcSubscription foo_sub = makeWatch(type_url, {}, foo_callbacks, resource_decoder_);

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
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  };

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x"});
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
      response->set_version_info("type_url_baz");
      response->set_nonce("type_url_bar");
      response->set_type_url("type_url_foo");
      EXPECT_CALL(*ttl_timer, disableTimer());
      grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  };

  FakeGrpcSubscription foo_sub = makeWatch("type_url_foo", {"x"});
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
  const std::string& type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          envoy::config::core::v3::ApiVersion::V3);
  grpc_mux_->start();

  // subscribe and unsubscribe so that the type is known to envoy
  {
    expectSendMessage(type_url, {"y"}, "", true);
    expectSendMessage(type_url, {}, "");
    FakeGrpcSubscription delete_immediately = makeWatch(type_url, {"y"});
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

TEST_F(GrpcMuxImplTest, BadLocalInfoEmptyClusterName) {
  EXPECT_CALL(local_info_, clusterName()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_THROW_WITH_MESSAGE(
      GrpcMuxSotw(
          std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_,
          local_info_, true),
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
          envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_,
          local_info_, true),
      EnvoyException,
      "ads: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

// DeltaDiscoveryResponse that comes in response to an on-demand request updates the watch with
// resource's name. The watch is initially created with an alias used in the on-demand request.
TEST_F(GrpcMuxImplTest, ConfigUpdateWithAliases) {
  std::unique_ptr<GrpcMuxDelta> grpc_mux = std::make_unique<GrpcMuxDelta>(
      std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
      envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_, local_info_,
      true);
  const std::string& type_url = Config::getTypeUrl<envoy::config::route::v3::VirtualHost>(
      envoy::config::core::v3::ApiVersion::V3);
  NiceMock<Grpc::MockAsyncStream> async_stream;
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::route::v3::VirtualHost>
      resource_decoder("prefix");
  grpc_mux->addWatch(type_url, {"prefix"}, callbacks, resource_decoder,
                     std::chrono::milliseconds(0), true);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream));
  grpc_mux->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  envoy::config::route::v3::VirtualHost vhost;
  vhost.set_name("vhost_1");
  vhost.add_domains("domain1.test");
  vhost.add_domains("domain2.test");

  response->add_resources()->mutable_resource()->PackFrom(vhost);
  response->mutable_resources()->at(0).set_name("prefix/vhost_1");
  response->mutable_resources()->at(0).add_aliases("prefix/domain1.test");
  response->mutable_resources()->at(0).add_aliases("prefix/domain2.test");

  EXPECT_CALL(callbacks, onConfigUpdate(_, _, "1")).Times(1);

  grpc_mux->onDiscoveryResponse(std::move(response), control_plane_stats_);
}

// DeltaDiscoveryResponse that comes in response to an on-demand request that couldn't be resolved
// will contain an empty Resource. The Resource's aliases field will be populated with the alias
// originally used in the request.
TEST_F(GrpcMuxImplTest, ConfigUpdateWithNotFoundResponse) {
  std::unique_ptr<GrpcMuxDelta> grpc_mux = std::make_unique<GrpcMuxDelta>(
      std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
      envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_, local_info_,
      true);
  const std::string& type_url = Config::getTypeUrl<envoy::config::route::v3::VirtualHost>(
      envoy::config::core::v3::ApiVersion::V3);
  NiceMock<Grpc::MockAsyncStream> async_stream;
  MockSubscriptionCallbacks callbacks;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::route::v3::VirtualHost>
      resource_decoder("prefix");

  grpc_mux->addWatch(type_url, {"prefix"}, callbacks, resource_decoder,
                     std::chrono::milliseconds(0), true);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream));
  grpc_mux->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  response->add_resources();
  response->mutable_resources()->at(0).set_name("prefix/not-found");
  response->mutable_resources()->at(0).add_aliases("prefix/domain1.test");

  EXPECT_CALL(callbacks, onConfigUpdate(_, _, "1")).Times(1);

  grpc_mux->onDiscoveryResponse(std::move(response), control_plane_stats_);
}

// Send discovery request with v2 resource type_url, receive discovery response with v3 resource
// type_url.
TEST_F(GrpcMuxImplTest, WatchV2ResourceV3) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.enable_type_url_downgrade_and_upgrade", "true"}});
  setup();

  InSequence s;
  const std::string& v2_type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  const std::string& v3_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          envoy::config::core::v3::ApiVersion::V3);
  auto foo_sub = makeWatch(v2_type_url, {}, callbacks_, resource_decoder_);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(v2_type_url, {}, "", true);
  grpc_mux_->start();
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(v3_type_url);
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
    expectSendMessage(v2_type_url, {}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }
}

// Send discovery request with v3 resource type_url, receive discovery response with v2 resource
// type_url.
TEST_F(GrpcMuxImplTest, WatchV3ResourceV2) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.enable_type_url_downgrade_and_upgrade", "true"}});
  setup();

  InSequence s;
  const std::string& v2_type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  const std::string& v3_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          envoy::config::core::v3::ApiVersion::V3);
  auto foo_sub = makeWatch(v3_type_url, {}, callbacks_, resource_decoder_);
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(v3_type_url, {}, "", true);
  grpc_mux_->start();

  {

    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(v2_type_url);
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
    expectSendMessage(v3_type_url, {}, "1");
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }
}

// Test that we simply ignore a message for an unknown type_url, with no ill effects.
TEST_F(GrpcMuxImplTest, DiscoveryResponseNonexistentSub) {
  setup();

  const std::string& type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          envoy::config::core::v3::ApiVersion::V3);
  grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder_, std::chrono::milliseconds(0),
                      false);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "", true);
  grpc_mux_->start();
  {
    auto unexpected_response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    unexpected_response->set_type_url("unexpected_type_url");
    unexpected_response->set_version_info("0");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "0")).Times(0);
    grpc_mux_->onDiscoveryResponse(std::move(unexpected_response), control_plane_stats_);
  }
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_version_info("1");
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("x");
  response->add_resources()->PackFrom(load_assignment);
  EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
      .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& resources,
                                          const std::string&) -> void {
        EXPECT_EQ(1, resources.size());
        EXPECT_TRUE(TestUtility::protoEqual(resources[0].get().resource(), load_assignment));
      }));
  expectSendMessage(type_url, {}, "1");
  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
}

} // namespace
} // namespace Config
} // namespace Envoy
