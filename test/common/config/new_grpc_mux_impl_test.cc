#include <memory>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/event/timer.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/new_grpc_mux_impl.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_mux/grpc_mux_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/config/v2_link_hacks.h"
#include "test/mocks/common.h"
#include "test/mocks/config/custom_config_validators.h"
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
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Config {
namespace {

enum class LegacyOrUnified { Legacy, Unified };

// We test some mux specific stuff below, other unit test coverage for singleton use of
// NewGrpcMuxImpl is provided in [grpc_]subscription_impl_test.cc.
class NewGrpcMuxImplTestBase : public testing::TestWithParam<LegacyOrUnified> {
public:
  NewGrpcMuxImplTestBase(LegacyOrUnified legacy_or_unified)
      : async_client_(new Grpc::MockAsyncClient()),
        config_validators_(std::make_unique<NiceMock<MockCustomConfigValidators>>()),
        resource_decoder_(std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
                              envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")),
        control_plane_stats_(Utility::generateControlPlaneStats(stats_)),
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)),
        should_use_unified_(legacy_or_unified == LegacyOrUnified::Unified) {}

  void setup() {
    if (isUnifiedMuxTest()) {
      grpc_mux_ = std::make_unique<XdsMux::GrpcMuxDelta>(
          std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_, rate_limit_settings_, local_info_, false, std::move(config_validators_));
      return;
    }
    grpc_mux_ = std::make_unique<NewGrpcMuxImpl>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, rate_limit_settings_, local_info_, std::move(config_validators_));
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names_subscribe,
                         const std::vector<std::string>& resource_names_unsubscribe,
                         const std::string& nonce = "",
                         const Protobuf::int32 error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
                         const std::string& error_message = "",
                         const std::map<std::string, std::string>& initial_resource_versions = {}) {
    API_NO_BOOST(envoy::service::discovery::v3::DeltaDiscoveryRequest) expected_request;
    expected_request.mutable_node()->CopyFrom(local_info_.node());
    for (const auto& resource : resource_names_subscribe) {
      expected_request.add_resource_names_subscribe(resource);
    }
    for (const auto& resource : resource_names_unsubscribe) {
      expected_request.add_resource_names_unsubscribe(resource);
    }
    for (const auto& v : initial_resource_versions) {
      (*expected_request.mutable_initial_resource_versions())[v.first] = v.second;
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

  void remoteClose() {
    if (isUnifiedMuxTest()) {
      dynamic_cast<XdsMux::GrpcMuxDelta*>(grpc_mux_.get())
          ->grpcStreamForTest()
          .onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
      return;
    }
    dynamic_cast<NewGrpcMuxImpl*>(grpc_mux_.get())
        ->grpcStreamForTest()
        .onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  }

  void onDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& response) {
    if (isUnifiedMuxTest()) {
      dynamic_cast<XdsMux::GrpcMuxDelta*>(grpc_mux_.get())
          ->onDiscoveryResponse(std::move(response), control_plane_stats_);
      return;
    }
    dynamic_cast<NewGrpcMuxImpl*>(grpc_mux_.get())
        ->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }

  void shutdownMux() {
    if (isUnifiedMuxTest()) {
      dynamic_cast<XdsMux::GrpcMuxDelta*>(grpc_mux_.get())->shutdown();
      return;
    }
    dynamic_cast<NewGrpcMuxImpl*>(grpc_mux_.get())->shutdown();
  }

  // the code is duplicated here, but all calls other than the check in return statement, return
  // different types.
  bool subscriptionExists(const std::string& type_url) const {
    if (isUnifiedMuxTest()) {
      auto* mux = dynamic_cast<XdsMux::GrpcMuxDelta*>(grpc_mux_.get());
      auto& subscriptions = mux->subscriptions();
      auto sub = subscriptions.find(type_url);
      return sub != subscriptions.end();
    }
    auto* mux = dynamic_cast<NewGrpcMuxImpl*>(grpc_mux_.get());
    auto& subscriptions = mux->subscriptions();
    auto sub = subscriptions.find(type_url);
    return sub != subscriptions.end();
  }

  bool isUnifiedMuxTest() const { return should_use_unified_; }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  CustomConfigValidatorsPtr config_validators_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::unique_ptr<GrpcMux> grpc_mux_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  Stats::TestUtil::TestStore stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  ControlPlaneStats control_plane_stats_;
  Stats::Gauge& control_plane_connected_state_;
  bool should_use_unified_;
};

class NewGrpcMuxImplTest : public NewGrpcMuxImplTestBase {
public:
  NewGrpcMuxImplTest() : NewGrpcMuxImplTestBase(GetParam()) {}
  Event::SimulatedTimeSystem time_system_;
};

INSTANTIATE_TEST_SUITE_P(NewGrpcMuxImplTest, NewGrpcMuxImplTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

// Validate behavior when dynamic context parameters are updated.
TEST_P(NewGrpcMuxImplTest, DynamicContextParameters) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  auto bar_sub = grpc_mux_->addWatch("bar", {}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, {});
  expectSendMessage("bar", {}, {});
  grpc_mux_->start();
  // Unknown type, shouldn't do anything.
  local_info_.context_provider_.update_cb_handler_.runCallbacks("baz");
  // Update to foo type should resend Node.
  expectSendMessage("foo", {}, {});
  local_info_.context_provider_.update_cb_handler_.runCallbacks("foo");
  // Update to bar type should resend Node.
  expectSendMessage("bar", {}, {});
  local_info_.context_provider_.update_cb_handler_.runCallbacks("bar");

  expectSendMessage("foo", {}, {"x", "y"});
}

// Validate cached nonces are cleared on reconnection.
// TODO (dmitri-d) remove this test when legacy implementations have been removed
// common mux functionality is tested in xds_grpc_mux_impl_test.cc
TEST_P(NewGrpcMuxImplTest, ReconnectionResetsNonceAndAcks) {
  Event::MockTimer* grpc_stream_retry_timer{new Event::MockTimer()};
  Event::MockTimer* ttl_mgr_timer{new NiceMock<Event::MockTimer>()};
  Event::TimerCb grpc_stream_retry_timer_cb;
  EXPECT_CALL(dispatcher_, createTimer_(_))
      .WillOnce(
          testing::DoAll(SaveArg<0>(&grpc_stream_retry_timer_cb), Return(grpc_stream_retry_timer)))
      // Happens when adding a type url watch.
      .WillRepeatedly(Return(ttl_mgr_timer));
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = grpc_mux_->addWatch(type_url, {"x", "y"}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Send on connection.
  expectSendMessage(type_url, {"x", "y"}, {});
  grpc_mux_->start();
  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("3000");
  response->set_nonce("111");
  auto add_response_resource = [](const std::string& name, const std::string& version,
                                  envoy::service::discovery::v3::DeltaDiscoveryResponse& response) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cla;
    cla.set_cluster_name(name);
    auto res = response.add_resources();
    res->set_name(name);
    res->mutable_resource()->PackFrom(cla);
    res->set_version(version);
  };
  add_response_resource("x", "2000", *response);
  add_response_resource("y", "3000", *response);
  // Pause EDS to allow the ACK to be cached.
  auto resume_eds = grpc_mux_->pause(type_url);
  onDiscoveryResponse(std::move(response));
  // Now disconnect.
  // Grpc stream retry timer will kick in and reconnection will happen.
  EXPECT_CALL(*grpc_stream_retry_timer, enableTimer(_, _))
      .WillOnce(Invoke(grpc_stream_retry_timer_cb));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // initial_resource_versions should contain client side all resource:version info.
  expectSendMessage(type_url, {"x", "y"}, {}, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    {{"x", "2000"}, {"y", "3000"}});
  remoteClose();

  expectSendMessage(type_url, {}, {"x", "y"});
}

// Validate resources are not sent on wildcard watch reconnection.
// Regression test of https://github.com/envoyproxy/envoy/issues/16063.
TEST_P(NewGrpcMuxImplTest, ReconnectionResetsWildcardSubscription) {
  Event::MockTimer* grpc_stream_retry_timer{new Event::MockTimer()};
  Event::MockTimer* ttl_mgr_timer{new NiceMock<Event::MockTimer>()};
  Event::TimerCb grpc_stream_retry_timer_cb;
  EXPECT_CALL(dispatcher_, createTimer_(_))
      .WillOnce(
          testing::DoAll(SaveArg<0>(&grpc_stream_retry_timer_cb), Return(grpc_stream_retry_timer)))
      // Happens when adding a type url watch.
      .WillRepeatedly(Return(ttl_mgr_timer));
  setup();
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Send a wildcard request on new connection.
  expectSendMessage(type_url, {}, {});
  grpc_mux_->start();

  // An helper function to create a response with a single load_assignment resource
  // (load_assignment's cluster_name will be updated).
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  auto create_response = [&load_assignment, &type_url](const std::string& name,
                                                       const std::string& version,
                                                       const std::string& nonce)
      -> std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse> {
    auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_system_version_info(version);
    response->set_nonce(nonce);
    auto res = response->add_resources();
    res->set_name(name);
    res->set_version(version);
    load_assignment.set_cluster_name(name);
    res->mutable_resource()->PackFrom(load_assignment);
    return response;
  };

  // Send a response with a single resource that should be received by Envoy,
  // followed by an ack with the nonce.
  {
    auto response = create_response("x", "1000", "111");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1000"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                            const Protobuf::RepeatedPtrField<std::string>&,
                                            const std::string&) {
          EXPECT_EQ(1, added_resources.size());
          EXPECT_TRUE(
              TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
        }));
    // Expect an ack with the nonce.
    expectSendMessage(type_url, {}, {}, "111");
    onDiscoveryResponse(std::move(response));
  }
  // Send another response with a different resource, but where EDS is paused.
  auto resume_eds = grpc_mux_->pause(type_url);
  {
    auto response = create_response("y", "2000", "222");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "2000"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                            const Protobuf::RepeatedPtrField<std::string>&,
                                            const std::string&) {
          EXPECT_EQ(1, added_resources.size());
          EXPECT_TRUE(
              TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
        }));
    // No ack reply is expected in this case, as EDS is suspended.
    onDiscoveryResponse(std::move(response));
  }

  // Now disconnect.
  // Grpc stream retry timer will kick in and reconnection will happen.
  EXPECT_CALL(*grpc_stream_retry_timer, enableTimer(_, _))
      .WillOnce(Invoke(grpc_stream_retry_timer_cb));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // initial_resource_versions should contain client side all resource:version info, and no
  // added resources because this is a wildcard request.
  expectSendMessage(type_url, {}, {}, "", Grpc::Status::WellKnownGrpcStatus::Ok, "",
                    {{"x", "1000"}, {"y", "2000"}});
  remoteClose();
  // Destruction of wildcard will not issue unsubscribe requests for the resources.
}

// Test that we simply ignore a message for an unknown type_url, with no ill effects.
TEST_P(NewGrpcMuxImplTest, DiscoveryResponseNonexistentSub) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto watch = grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder_, {});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  {
    auto unexpected_response =
        std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    unexpected_response->set_type_url(type_url);
    unexpected_response->set_system_version_info("0");
    // empty response should call onConfigUpdate on wildcard watch
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "0"));
    onDiscoveryResponse(std::move(unexpected_response));
  }
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_system_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->mutable_resource()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                            const Protobuf::RepeatedPtrField<std::string>&,
                                            const std::string&) {
          EXPECT_EQ(1, added_resources.size());
          EXPECT_TRUE(
              TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
        }));
    onDiscoveryResponse(std::move(response));
  }
}

// DeltaDiscoveryResponse that comes in response to an on-demand request updates the watch with
// resource's name. The watch is initially created with an alias used in the on-demand request.
TEST_P(NewGrpcMuxImplTest, ConfigUpdateWithAliases) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().VirtualHost;
  SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  auto watch = grpc_mux_->addWatch(type_url, {"prefix"}, callbacks_, resource_decoder_, options);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");
  response->mutable_control_plane()->set_identifier("HAL 9000");

  envoy::config::route::v3::VirtualHost vhost;
  vhost.set_name("vhost_1");
  vhost.add_domains("domain1.test");
  vhost.add_domains("domain2.test");

  response->add_resources()->mutable_resource()->PackFrom(vhost);
  response->mutable_resources()->at(0).set_name("prefix/vhost_1");
  response->mutable_resources()->at(0).add_aliases("prefix/domain1.test");
  response->mutable_resources()->at(0).add_aliases("prefix/domain2.test");

  EXPECT_LOG_CONTAINS("debug", "for " + type_url + " from HAL 9000",
                      onDiscoveryResponse(std::move(response)));
  EXPECT_TRUE(subscriptionExists(type_url));
  watch->update({});

  EXPECT_EQ("HAL 9000", stats_.textReadout("control_plane.identifier").value());
}

// DeltaDiscoveryResponse that comes in response to an on-demand request that couldn't be resolved
// will contain an empty Resource. The Resource's aliases field will be populated with the alias
// originally used in the request.
TEST_P(NewGrpcMuxImplTest, ConfigUpdateWithNotFoundResponse) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().VirtualHost;
  SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  auto watch = grpc_mux_->addWatch(type_url, {"prefix"}, callbacks_, resource_decoder_, options);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  response->add_resources();
  response->mutable_resources()->at(0).set_name("not-found");
  response->mutable_resources()->at(0).add_aliases("prefix/domain1.test");
}

// Validate basic gRPC mux subscriptions to xdstp:// glob collections.
TEST_P(NewGrpcMuxImplTest, XdsTpGlobCollection) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  xds::core::v3::ContextParams context_params;
  (*context_params.mutable_params())["foo"] = "bar";
  EXPECT_CALL(local_info_.context_provider_, nodeContext()).WillOnce(ReturnRef(context_params));
  // We verify that the gRPC mux normalizes the context parameter order below.
  SubscriptionOptions options;
  options.add_xdstp_node_context_params_ = true;
  auto watch = grpc_mux_->addWatch(
      type_url,
      {"xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/bar/*?thing=some&some=thing"},
      callbacks_, resource_decoder_, options);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("ignore");
  auto* resource = response->add_resources();
  resource->set_name("xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/bar/"
                     "a?foo=bar&some=thing&thing=some");
  resource->mutable_resource()->PackFrom(load_assignment);
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1"))
      .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                          const Protobuf::RepeatedPtrField<std::string>&,
                                          const std::string&) {
        EXPECT_EQ(1, added_resources.size());
        EXPECT_TRUE(TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
      }));
  onDiscoveryResponse(std::move(response));
}

// Validate basic gRPC mux subscriptions to xdstp:// singletons.
TEST_P(NewGrpcMuxImplTest, XdsTpSingleton) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  EXPECT_CALL(local_info_.context_provider_, nodeContext()).Times(0);
  // We verify that the gRPC mux normalizes the context parameter order below. Node context
  // parameters are skipped.
  SubscriptionOptions options;
  auto watch = grpc_mux_->addWatch(type_url,
                                   {
                                       "xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                                       "bar/baz?thing=some&some=thing",
                                       "opaque_resource_name",
                                       "xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                                       "bar/blah?thing=some&some=thing",
                                   },
                                   callbacks_, resource_decoder_, options);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("ignore");
  {
    auto* resource = response->add_resources();
    resource->set_name(
        "xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/bar/baz?some=thing&thing=some");
    resource->mutable_resource()->PackFrom(load_assignment);
  }
  {
    auto* resource = response->add_resources();
    resource->set_name("opaque_resource_name");
    resource->mutable_resource()->PackFrom(load_assignment);
  }
  {
    auto* resource = response->add_resources();
    resource->set_name("xdstp://foo/envoy.config.endpoint.v3.ClusterLoadAssignment/bar/"
                       "blah?some=thing&thing=some");
    resource->mutable_resource()->PackFrom(load_assignment);
  }
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1"))
      .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                          const Protobuf::RepeatedPtrField<std::string>&,
                                          const std::string&) {
        EXPECT_EQ(3, added_resources.size());
        EXPECT_TRUE(TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
        EXPECT_TRUE(TestUtility::protoEqual(added_resources[1].get().resource(), load_assignment));
        EXPECT_TRUE(TestUtility::protoEqual(added_resources[2].get().resource(), load_assignment));
      }));
  onDiscoveryResponse(std::move(response));
}

TEST_P(NewGrpcMuxImplTest, RequestOnDemandUpdate) {
  setup();

  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, {});
  grpc_mux_->start();

  expectSendMessage("foo", {"z"}, {});
  grpc_mux_->requestOnDemandUpdate("foo", {"z"});

  expectSendMessage("foo", {}, {"x", "y"});
}

TEST_P(NewGrpcMuxImplTest, Shutdown) {
  setup();
  InSequence s;
  auto foo_sub = grpc_mux_->addWatch("foo", {"x", "y"}, callbacks_, resource_decoder_, {});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, {});
  grpc_mux_->start();

  shutdownMux();
  auto bar_sub = grpc_mux_->addWatch("bar", {"z"}, callbacks_, resource_decoder_, {});
  // We do not expect any messages to be sent here as the mux has been shutdown
  // There won't be any unsubscribe messages for the legacy mux either for the same reason
}

} // namespace
} // namespace Config
} // namespace Envoy
