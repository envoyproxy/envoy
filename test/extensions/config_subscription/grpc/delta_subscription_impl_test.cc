#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/config/api_version.h"

#include "test/extensions/config_subscription/grpc/delta_subscription_test_harness.h"

namespace Envoy {
namespace Config {
namespace {

using ::testing::Return;

class DeltaSubscriptionImplTest : public DeltaSubscriptionTestHarness,
                                  public testing::TestWithParam<LegacyOrUnified> {
protected:
  DeltaSubscriptionImplTest() : DeltaSubscriptionTestHarness(GetParam()) {};

  // We need to destroy the subscription before the test's destruction, because the subscription's
  // destructor removes its watch from the NewGrpcMuxImpl, and that removal process involves
  // some things held by the test fixture.
  void TearDown() override { doSubscriptionTearDown(); }
};

INSTANTIATE_TEST_SUITE_P(DeltaSubscriptionImplTest, DeltaSubscriptionImplTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

TEST_P(DeltaSubscriptionImplTest, UpdateResourcesCausesRequest) {
  startSubscription({"name1", "name2", "name3"});
  expectSendMessage({"name4"}, {"name1", "name2"}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->updateResourceInterest({"name3", "name4"});
  expectSendMessage({"name1", "name2"}, {}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->updateResourceInterest({"name1", "name2", "name3", "name4"});
  expectSendMessage({}, {"name1", "name2"}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->updateResourceInterest({"name3", "name4"});
  expectSendMessage({"name1", "name2"}, {}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->updateResourceInterest({"name1", "name2", "name3", "name4"});
  expectSendMessage({}, {"name1", "name2", "name3"}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->updateResourceInterest({"name4"});
}

// Checks that after a pause(), no requests are sent until resume().
// Also demonstrates the collapsing of subscription interest updates into a single
// request. (This collapsing happens any time multiple updates arrive before a request
// can be sent, not just with pausing: rate limiting or a down gRPC stream would also do it).
TEST_P(DeltaSubscriptionImplTest, PauseHoldsRequest) {
  startSubscription({"name1", "name2", "name3"});
  auto resume_sub = subscription_->pause();
  // If nested pause wasn't handled correctly, the single expectedSendMessage below would be
  // insufficient.
  auto nested_resume_sub = subscription_->pause();

  expectSendMessage({"name4"}, {"name1", "name2"}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  // If not for the pause, these updates would make the expectSendMessage fail due to too many
  // messages being sent.
  subscription_->updateResourceInterest({"name3", "name4"});
  subscription_->updateResourceInterest({"name1", "name2", "name3", "name4"});
  subscription_->updateResourceInterest({"name3", "name4"});
  subscription_->updateResourceInterest({"name1", "name2", "name3", "name4"});
  subscription_->updateResourceInterest({"name3", "name4"});
}

TEST_P(DeltaSubscriptionImplTest, ResponseCausesAck) {
  startSubscription({"name1"});
  deliverConfigUpdate({"name1"}, "someversion", true);
}

// Regression test for on-demand updates: a resource requested via requestOnDemandUpdate() that was
// not part of the initial (non-wildcard) subscription must still be routed to the subscription's
// callbacks. Previously requestOnDemandUpdate() only updated the subscription sent to the server,
// not the watch-map routing, so the response for such a resource was silently dropped.
TEST_P(DeltaSubscriptionImplTest, OnDemandUpdateRoutesResponseToWatch) {
  // A concrete (non-wildcard) initial subscription, as ODCDS uses.
  startSubscription({"name1"});
  deliverConfigUpdate({"name1"}, "version1", true);

  // Request a brand-new resource on demand. This must subscribe on the wire...
  expectSendMessage({"name2"}, {}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->requestOnDemandUpdate({"name2"});
  Mock::VerifyAndClearExpectations(&async_stream_);

  // ...and register watch-map interest, so the server's response for it reaches the callbacks.
  const std::string version = "version2";
  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
  response->set_nonce(last_response_nonce_);
  response->set_system_version_info(version);
  response->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("name2");
  auto* resource = response->add_resources();
  resource->set_name("name2");
  resource->set_version(version);
  std::ignore = resource->mutable_resource()->PackFrom(load_assignment);

  bool delivered_name2 = false;
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, version))
      .WillOnce(Invoke([&delivered_name2](const std::vector<DecodedResourceRef>& added_resources,
                                          const Protobuf::RepeatedPtrField<std::string>&,
                                          const std::string&) {
        EXPECT_EQ(1, added_resources.size());
        if (!added_resources.empty()) {
          EXPECT_EQ("name2", added_resources[0].get().name());
        }
        delivered_name2 = true;
        return absl::OkStatus();
      }));
  expectSendMessage({}, version); // ACK
  onDiscoveryResponse(std::move(response));
  EXPECT_TRUE(delivered_name2);
}

// Regression test for the VHDS-style pattern: a subscription that accepts a glob prefix for routing
// (accept("<rc1>/*"), as VHDS does for its route configuration) then requests, on demand, a
// resource that is NOT covered by that glob (here under a different "<rc2>/" prefix). The response
// for the on-demand resource must still be routed to the callbacks. Before the fix,
// requestOnDemandUpdate() only updated the wire subscription; a resource matched by neither the
// glob nor a start()-time name matched no watch and was silently dropped.
TEST_P(DeltaSubscriptionImplTest, OnDemandUpdateOutsideAcceptGlobRoutesResponse) {
  startSubscription({"name1"});
  deliverConfigUpdate({"name1"}, "version1", true);

  // Accept everything under "rc1/" for routing only (no wire subscription); mirrors VHDS.
  subscription_->accept({"rc1/*"});

  // Request an on-demand resource under a DIFFERENT prefix ("rc2/"), i.e. not covered by the glob.
  // This must subscribe on the wire...
  expectSendMessage({"rc2/vhost"}, {}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  subscription_->requestOnDemandUpdate({"rc2/vhost"});
  Mock::VerifyAndClearExpectations(&async_stream_);

  // ...and register watch-map interest, so the server's response for it reaches the callbacks even
  // though it matches neither the "rc1/*" glob nor the initial "name1" subscription.
  const std::string version = "version2";
  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
  response->set_nonce(last_response_nonce_);
  response->set_system_version_info(version);
  response->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("rc2/vhost");
  auto* resource = response->add_resources();
  resource->set_name("rc2/vhost");
  resource->set_version(version);
  std::ignore = resource->mutable_resource()->PackFrom(load_assignment);

  bool delivered = false;
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, version))
      .WillOnce(
          Invoke([&delivered](const std::vector<DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
            EXPECT_EQ(1, added_resources.size());
            if (!added_resources.empty()) {
              EXPECT_EQ("rc2/vhost", added_resources[0].get().name());
            }
            delivered = true;
            return absl::OkStatus();
          }));
  expectSendMessage({}, version); // ACK
  onDiscoveryResponse(std::move(response));
  EXPECT_TRUE(delivered);
}

// accept() may be called before start() (as VHDS does): the patterns are buffered and applied when
// the watch is created in start(), before the gRPC stream starts, so the routing filter is in
// effect before any resource can be delivered. Verifies the buffered accept() is actually applied.
TEST_P(DeltaSubscriptionImplTest, AcceptBeforeStartIsApplied) {
  // Declare the accept glob BEFORE start(); the watch does not exist yet, so this is buffered.
  subscription_->accept({"rc1/*"});
  startSubscription({"name1"});
  deliverConfigUpdate({"name1"}, "version1", true);

  // A resource under the accepted "rc1/*" glob is routed even though it was never explicitly
  // requested -- proving the buffered accept() was applied during start().
  const std::string version = "version2";
  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
  response->set_nonce(last_response_nonce_);
  response->set_system_version_info(version);
  response->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
  envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
  load_assignment.set_cluster_name("rc1/foo");
  auto* resource = response->add_resources();
  resource->set_name("rc1/foo");
  resource->set_version(version);
  std::ignore = resource->mutable_resource()->PackFrom(load_assignment);

  bool delivered = false;
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, version))
      .WillOnce(
          Invoke([&delivered](const std::vector<DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
            EXPECT_EQ(1, added_resources.size());
            if (!added_resources.empty()) {
              EXPECT_EQ("rc1/foo", added_resources[0].get().name());
            }
            delivered = true;
            return absl::OkStatus();
          }));
  expectSendMessage({}, version); // ACK
  onDiscoveryResponse(std::move(response));
  EXPECT_TRUE(delivered);
}

// Checks that after a pause(), no ACK requests are sent until resume(), but that after the
// resume, *all* ACKs that arrived during the pause are sent (in order).
TEST_P(DeltaSubscriptionImplTest, PauseQueuesAcks) {
  startSubscription({"name1", "name2", "name3"});
  auto resume_sub = subscription_->pause();
  // The server gives us our first version of resource name1.
  // subscription_ now wants to ACK name1 (but can't due to pause).
  {
    auto message = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name1");
    resource->set_version("version1A");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version1A"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    onDiscoveryResponse(std::move(message));
  }
  // The server gives us our first version of resource name2.
  // subscription_ now wants to ACK name1 and then name2 (but can't due to pause).
  {
    auto message = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name2");
    resource->set_version("version2A");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version2A"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    onDiscoveryResponse(std::move(message));
  }
  // The server gives us an updated version of resource name1.
  // subscription_ now wants to ACK name1A, then name2, then name1B (but can't due to pause).
  {
    auto message = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name1");
    resource->set_version("version1B");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version1B"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TestTypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    onDiscoveryResponse(std::move(message));
  }
  // All ACK sendMessage()s will happen upon calling resume().
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _))
      .WillRepeatedly(Invoke([this](Buffer::InstancePtr& buffer, bool) {
        API_NO_BOOST(envoy::service::discovery::v3::DeltaDiscoveryRequest) message;
        EXPECT_TRUE(Grpc::Common::parseBufferInstance(std::move(buffer), message));
        const std::string nonce = message.response_nonce();
        if (!nonce.empty()) {
          nonce_acks_sent_.push(nonce);
        }
      }));
  // DeltaSubscriptionTestHarness's dtor will check that all ACKs were sent with the correct nonces,
  // in the correct order.
}

class DeltaSubscriptionNoGrpcStreamTest : public testing::TestWithParam<LegacyOrUnified> {};
INSTANTIATE_TEST_SUITE_P(DeltaSubscriptionNoGrpcStreamTest, DeltaSubscriptionNoGrpcStreamTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

TEST_P(DeltaSubscriptionNoGrpcStreamTest, NoGrpcStream) {
  Stats::IsolatedStoreImpl stats_store;
  SubscriptionStats stats(Utility::generateStats(*stats_store.rootScope()));

  envoy::config::core::v3::Node node;
  node.set_id("fo0");
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  EXPECT_CALL(local_info, node()).WillRepeatedly(testing::ReturnRef(node));

  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Envoy::Config::RateLimitSettings rate_limit_settings;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks;
  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<NiceMock<Config::MockOpaqueResourceDecoder>>());
  auto* async_client = new Grpc::MockAsyncClient();

  const Protobuf::MethodDescriptor* method_descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.endpoint.v3.EndpointDiscoveryService.StreamEndpoints");
  GrpcMuxSharedPtr xds_context;
  auto backoff_strategy = std::make_unique<JitteredExponentialBackOffStrategy>(
      SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random);

  GrpcMuxContext grpc_mux_context{
      /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(async_client),
      /*failover_async_client_=*/nullptr,
      /*dispatcher_=*/dispatcher,
      /*service_method_=*/*method_descriptor,
      /*local_info_=*/local_info,
      /*rate_limit_settings_=*/rate_limit_settings,
      /*scope_=*/*stats_store.rootScope(),
      /*config_validators_=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
      /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
      /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
      /*backoff_strategy_=*/std::move(backoff_strategy),
      /*target_xds_authority_=*/"",
      /*eds_resources_cache_=*/nullptr,
      /*skip_subsequent_node_=*/false};
  if (GetParam() == LegacyOrUnified::Unified) {
    xds_context = std::make_shared<Config::XdsMux::GrpcMuxDelta>(grpc_mux_context);
  } else {
    xds_context = std::make_shared<NewGrpcMuxImpl>(grpc_mux_context);
  }

  GrpcSubscriptionImplPtr subscription = std::make_unique<GrpcSubscriptionImpl>(
      xds_context, callbacks, resource_decoder, stats,
      Config::TestTypeUrl::get().ClusterLoadAssignment, dispatcher,
      std::chrono::milliseconds(12345), false, SubscriptionOptions());

  EXPECT_CALL(*async_client, startRaw(_, _, _, _)).WillOnce(Return(nullptr));

  subscription->start({"name1"});
  subscription->updateResourceInterest({"name1", "name2"});
}

} // namespace
} // namespace Config
} // namespace Envoy
