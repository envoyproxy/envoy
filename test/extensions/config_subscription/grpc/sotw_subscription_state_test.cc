#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"

#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_symbol_table.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/config_subscription/grpc/xds_mux/sotw_subscription_state.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/enum_test_utils.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::An;
using testing::Contains;
using testing::IsSubstring;
using testing::NiceMock;
using testing::SizeIs;
using testing::Throw;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Config {
namespace {

constexpr char RESOURCE_VERSION[] = "555";
constexpr char BAD_CLA_RESOURCE_NAME[] = "bad_cla";

// A test implementation of the XdsResourcesDelegate for the purposes of this test.
class TestXdsResourcesDelegate : public XdsResourcesDelegate {
public:
  void onConfigUpdated(const XdsSourceId& /*source_id*/,
                       const std::vector<DecodedResourceRef>& /*resources*/) override {}

  void onResourceLoadFailed(const Config::XdsSourceId& /*source_id*/,
                            const std::string& resource_name,
                            const std::optional<EnvoyException>& /*exception*/) override {
    failed_resource_names_.push_back(resource_name);
  }

  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const Config::XdsSourceId& /*source_id*/,
               const absl::flat_hash_set<std::string>& resource_names) const override {
    if (throws_ex_) {
      throw EnvoyException("intended exception thrown");
    }

    // If we get a wildcard request, generate CLA resources for the default {"name1", "name2",
    // "name3"}.
    absl::flat_hash_set<std::string> resources_to_generate{resource_names.begin(),
                                                           resource_names.end()};
    if (resources_to_generate.empty() ||
        (resources_to_generate.size() == 1 &&
         resources_to_generate.contains(Envoy::Config::Wildcard))) {
      resources_to_generate = {"name1", "name2", "name3"};
    }

    std::vector<envoy::service::discovery::v3::Resource> resources;
    resources.reserve(resource_names.size());
    for (const std::string& resource_name : resources_to_generate) {
      envoy::config::endpoint::v3::ClusterLoadAssignment cla;
      cla.set_cluster_name(resource_name);
      if (resource_name == std::string(BAD_CLA_RESOURCE_NAME)) {
        // If the bad resource is requested, set an invalid enum value (1000) for the Policy's
        // drop percentage denominator.
        cla.mutable_policy()->add_drop_overloads()->mutable_drop_percentage()->set_denominator(
            uncheckedEnumCastForTest<envoy::type::v3::FractionalPercent_DenominatorType>(1000));
      }

      envoy::service::discovery::v3::Resource resource;
      std::ignore = resource.mutable_resource()->PackFrom(cla);
      resource.set_name(cla.cluster_name());
      resource.set_version(RESOURCE_VERSION);
      resources.emplace_back(std::move(resource));
    }
    return resources;
  }

  std::vector<std::string>& failedResourceNames() { return failed_resource_names_; }

  void setThrowsException() { throws_ex_ = true; }

private:
  std::vector<std::string> failed_resource_names_;
  bool throws_ex_{false};
};

class SotwSubscriptionStateTest : public testing::TestWithParam<bool> {
protected:
  SotwSubscriptionStateTest()
      : resource_decoder_(
            std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
                envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")) {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.compress_xds_resource_names", GetParam() ? "true" : "false"}});
    ttl_timer_ = new Event::MockTimer(&dispatcher_);
    xds_resources_delegate_ = std::make_unique<TestXdsResourcesDelegate>();
    state_ = std::make_unique<XdsMux::SotwSubscriptionState>(
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(), callbacks_,
        dispatcher_, resource_decoder_, /*xds_config_tracker=*/XdsConfigTrackerOptRef(),
        *xds_resources_delegate_, /*target_xds_authority=*/"some_random_xds_server");
    state_->updateSubscriptionInterest({"name1", "name2", "name3"}, {});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
  }

  std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>
  getNextDiscoveryRequestAckless() {
    return state_->getNextRequestAckless();
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
                  const envoy::config::endpoint::v3::ClusterLoadAssignment& cla) {
    envoy::service::discovery::v3::Resource resource;
    std::ignore = resource.mutable_resource()->PackFrom(cla);
    resource.mutable_ttl()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(ttl.count()));
    resource.set_name(cla.cluster_name());
    return resource;
  }

  const envoy::config::endpoint::v3::ClusterLoadAssignment
  resource(const std::string& cluster_name) {
    envoy::config::endpoint::v3::ClusterLoadAssignment resource;
    resource.set_cluster_name(cluster_name);
    return resource;
  }

  UpdateAck deliverDiscoveryResponse(const std::vector<std::string>& resource_names,
                                     const std::string& version_info, const std::string& nonce) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version_info);
    response.set_nonce(nonce);
    response.set_type_url(Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>());
    for (const auto& resource_name : resource_names) {
      std::ignore = response.add_resources()->PackFrom(resource(resource_name));
    }
    EXPECT_CALL(callbacks_,
                onConfigUpdate(An<const std::vector<DecodedResourcePtr>&>(), version_info))
        .WillOnce([](const std::vector<DecodedResourcePtr>& resources, const std::string&) {
          for (size_t i = 0; i < resources.size(); ++i) {
            EXPECT_NE(resources[i]->resource().GetArena(), nullptr);
            if (i > 0) {
              EXPECT_EQ(resources[i]->resource().GetArena(), resources[0]->resource().GetArena());
            }
          }
        });
    return state_->handleResponse(response);
  }

  UpdateAck
  deliverDiscoveryResponseWithTtlResource(const envoy::service::discovery::v3::Resource& resource,
                                          const std::string& version_info,
                                          const std::string& nonce) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version_info);
    response.set_nonce(nonce);
    response.set_type_url(Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>());
    std::ignore = response.add_resources()->PackFrom(resource);
    EXPECT_CALL(callbacks_,
                onConfigUpdate(An<const std::vector<DecodedResourcePtr>&>(), version_info));
    return state_->handleResponse(response);
  }

  UpdateAck deliverBadDiscoveryResponse(const std::string& version_info, const std::string& nonce) {
    envoy::service::discovery::v3::DiscoveryResponse message;
    message.set_version_info(version_info);
    message.set_nonce(nonce);
    EXPECT_CALL(callbacks_, onConfigUpdate(An<const std::vector<DecodedResourcePtr>&>(), _))
        .WillOnce(Throw(EnvoyException("oh no")));
    return state_->handleResponse(message);
  }

  void throwExceptionInDelegate() { xds_resources_delegate_->setThrowsException(); }

  TestScopedRuntime scoped_runtime_;
  NiceMock<MockUntypedConfigUpdateCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* ttl_timer_;
  std::unique_ptr<TestXdsResourcesDelegate> xds_resources_delegate_;
  // We start out interested in three resources: name1, name2, and name3.
  std::unique_ptr<XdsMux::SotwSubscriptionState> state_;
};

INSTANTIATE_TEST_SUITE_P(SotwSubscriptionStateTest, SotwSubscriptionStateTest, testing::Bool());

// Basic gaining/losing interest in resources should lead to changes in subscriptions.
TEST_P(SotwSubscriptionStateTest, SubscribeAndUnsubscribe) {
  {
    state_->updateSubscriptionInterest({"name4"}, {"name1"});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name2", "name3", "name4"));
  }
  {
    state_->updateSubscriptionInterest({"name1"}, {"name3", "name4"});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2"));
  }
}

// Unlike delta, if SotW gets multiple interest updates before being able to send a request, they
// all collapse to a single update. However, even if the updates all cancel each other out, there
// still will be a request generated. All of the following tests explore different such cases.
TEST_P(SotwSubscriptionStateTest, RemoveThenAdd) {
  state_->updateSubscriptionInterest({}, {"name3"});
  state_->updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_P(SotwSubscriptionStateTest, AddThenRemove) {
  state_->updateSubscriptionInterest({"name4"}, {});
  state_->updateSubscriptionInterest({}, {"name4"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_P(SotwSubscriptionStateTest, AddRemoveAdd) {
  state_->updateSubscriptionInterest({"name4"}, {});
  state_->updateSubscriptionInterest({}, {"name4"});
  state_->updateSubscriptionInterest({"name4"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(),
              UnorderedElementsAre("name1", "name2", "name3", "name4"));
}

TEST_P(SotwSubscriptionStateTest, RemoveAddRemove) {
  state_->updateSubscriptionInterest({}, {"name3"});
  state_->updateSubscriptionInterest({"name3"}, {});
  state_->updateSubscriptionInterest({}, {"name3"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2"));
}

TEST_P(SotwSubscriptionStateTest, BothAddAndRemove) {
  state_->updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  state_->updateSubscriptionInterest({"name1", "name2", "name3"}, {"name4"});
  state_->updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name4"));
}

TEST_P(SotwSubscriptionStateTest, CumulativeUpdates) {
  state_->updateSubscriptionInterest({"name4"}, {});
  state_->updateSubscriptionInterest({"name5"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(),
              UnorderedElementsAre("name1", "name2", "name3", "name4", "name5"));
}

TEST_P(SotwSubscriptionStateTest, LastUpdateNonceAndVersionUsed) {
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse({"name1", "name2"}, "version1", "nonce1");
  state_->updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ("nonce1", cur_request->response_nonce());
  EXPECT_EQ("version1", cur_request->version_info());
}

// Validates that the last update version info is kept, and that nonce is reset
// after reconnecting to the server.
TEST_P(SotwSubscriptionStateTest, LastUpdateNonceAndVersionAfterStreamRefresh) {
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse({"name1", "name2"}, "version1", "nonce1");
  state_->updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ("nonce1", cur_request->response_nonce());
  EXPECT_EQ("version1", cur_request->version_info());
  // Reconnect the stream.
  state_->markStreamFresh(true);
  cur_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ("", cur_request->response_nonce());
  EXPECT_EQ("version1", cur_request->version_info());
}

// Verifies that a sequence of good and bad responses from the server all get the appropriate
// ACKs/NACKs from Envoy.
TEST_P(SotwSubscriptionStateTest, AckGenerated) {
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2"}, "version1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response updates 1 and 2, and adds 3.
  {
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2", "name3"}, "version2", "nonce2");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update all 3, and so should produce a NACK.
  {
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverBadDiscoveryResponse("version3", "nonce3");
    EXPECT_EQ("nonce3", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The last response successfully updates all 3.
  {
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2", "name3"}, "version4", "nonce4");
    EXPECT_EQ("nonce4", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
}

TEST_P(SotwSubscriptionStateTest, CheckUpdatePending) {
  // Note that the test fixture ctor causes the first request to be "sent", so we start in the
  // middle of a stream, with our initially interested resources having been requested already.
  EXPECT_FALSE(state_->subscriptionUpdatePending());
  state_->updateSubscriptionInterest({}, {}); // no change
  EXPECT_FALSE(state_->subscriptionUpdatePending());
  state_->markStreamFresh(true);
  EXPECT_TRUE(state_->subscriptionUpdatePending());  // no change, BUT fresh stream
  state_->updateSubscriptionInterest({}, {"name3"}); // one removed
  EXPECT_TRUE(state_->subscriptionUpdatePending());
  state_->updateSubscriptionInterest({"name3"}, {}); // one added
  EXPECT_TRUE(state_->subscriptionUpdatePending());
}

TEST_P(SotwSubscriptionStateTest, HandleEstablishmentFailure) {
  // Although establishment failure is not supposed to cause an onConfigUpdateFailed() on the
  // ultimate actual subscription callbacks, the callbacks reference held is actually to
  // the WatchMap, which then calls GrpcSubscriptionImpl(s). It is the GrpcSubscriptionImpl
  // that will decline to pass on an onConfigUpdateFailed(ConnectionFailure).
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  // The XdsResourcesDelegate supplies the 3 requested resources, so we expect an onConfigUpdate
  // callback.
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(SizeIs(3)),
                             std::string(RESOURCE_VERSION)));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  state_->handleEstablishmentFailure();
  auto next_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ(next_request->version_info(), RESOURCE_VERSION);
  // No nonce, since the version didn't come from an xDS server.
  EXPECT_TRUE(next_request->response_nonce().empty());
}

TEST_P(SotwSubscriptionStateTest, HandleEstablishmentFailureWithInvalidResource) {
  // Add "bad_cla" to the subscribed resources, as it will return a ClusterLoadAssignment with an
  // invalid enum from the xDS delegate.
  const std::string bad_resource_name{BAD_CLA_RESOURCE_NAME};
  state_->updateSubscriptionInterest({bad_resource_name}, {});
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  // The XdsResourcesDelegate supplies 3 of the requested resources, but not the 4th "bad_cla"
  // resource.
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(SizeIs(3)),
                             std::string(RESOURCE_VERSION)));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  state_->handleEstablishmentFailure();
  EXPECT_THAT(xds_resources_delegate_->failedResourceNames(),
              AllOf(SizeIs(1), Contains(bad_resource_name)));
  auto next_request = getNextDiscoveryRequestAckless();
  // There was an invalid resource, so the last known good version won't be set.
  EXPECT_TRUE(next_request->version_info().empty());
  EXPECT_TRUE(next_request->response_nonce().empty());
}

TEST_P(SotwSubscriptionStateTest, HandleEstablishmentFailureWithDelegateThrowingException) {
  // Tell the test to throw an exception in the xDS delegate getResources() call.
  throwExceptionInDelegate();
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  // The XdsResourcesDelegate throws an exception, so onConfigUpdate is not called on the callbacks.
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(), _))
      .Times(0);
  state_->handleEstablishmentFailure();
  auto next_request = getNextDiscoveryRequestAckless();
  // The xDS delegate threw an exception, so the last known good version won't be set.
  EXPECT_TRUE(next_request->version_info().empty());
  EXPECT_TRUE(next_request->response_nonce().empty());
}

TEST_P(SotwSubscriptionStateTest, HandleEstablishmentFailureWithEmptyRequestedResourceNames) {
  // Make the request resource names list the empty set, to treat it like a wild card.
  state_->updateSubscriptionInterest({}, {"name1", "name2", "name3"});
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  // The XdsResourcesDelegate supplies the 3 resources it contains, since it's a wildcard request.
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(SizeIs(3)),
                             std::string(RESOURCE_VERSION)));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  state_->handleEstablishmentFailure();
  auto next_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ(next_request->version_info(), RESOURCE_VERSION);
  // No nonce, since the version didn't come from an xDS server.
  EXPECT_TRUE(next_request->response_nonce().empty());
}

TEST_P(SotwSubscriptionStateTest, HandleEstablishmentFailureWithWildcardResourceName) {
  // Make the request resource names list the wildcard entry only.
  state_->updateSubscriptionInterest({"*"}, {"name1", "name2", "name3"});
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  // The XdsResourcesDelegate supplies the 3 resources it contains, since it's a wildcard request.
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(SizeIs(3)),
                             std::string(RESOURCE_VERSION)));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  state_->handleEstablishmentFailure();
  auto next_request = getNextDiscoveryRequestAckless();
  EXPECT_EQ(next_request->version_info(), RESOURCE_VERSION);
  // No nonce, since the version didn't come from an xDS server.
  EXPECT_TRUE(next_request->response_nonce().empty());
}

TEST_P(SotwSubscriptionStateTest, ResourceTTL) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(std::chrono::milliseconds(0));
  {
    EXPECT_CALL(*ttl_timer_, enabled());
    EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(1000), _));
    deliverDiscoveryResponseWithTtlResource(
        resourceWithTtl(std::chrono::seconds(1), resource("name1")), "debug1", "nonce1");
  }

  {
    // Increase the TTL.
    EXPECT_CALL(*ttl_timer_, enabled());
    EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(2000), _));
    deliverDiscoveryResponseWithTtlResource(
        resourceWithTtl(std::chrono::seconds(2), resource("name1")), "debug1", "nonce1");
  }

  {
    // Refresh the TTL with a heartbeat. The resource should not be passed to the update callbacks.
    EXPECT_CALL(*ttl_timer_, enabled());
    deliverDiscoveryResponseWithTtlResource(heartbeatResource(std::chrono::seconds(2), "name1"),
                                            "debug1", "nonce1");
  }

  // Remove the TTL.
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse({"name1"}, "version1", "nonce1");

  // Add back the TTL.
  EXPECT_CALL(*ttl_timer_, enabled());
  EXPECT_CALL(*ttl_timer_, enableTimer(_, _));
  deliverDiscoveryResponseWithTtlResource(
      resourceWithTtl(std::chrono::seconds(2), resource("name1")), "debug1", "nonce1");

  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  time_system.setSystemTime(std::chrono::seconds(2));

  // Invoke the TTL.
  ttl_timer_->invokeCallback();
}

TEST_P(SotwSubscriptionStateTest, TypeUrlMismatch) {
  envoy::service::discovery::v3::DiscoveryResponse response;
  response.set_version_info("version1");
  response.set_nonce("nonce1");
  response.set_type_url("badtypeurl");
  std::ignore = response.add_resources()->PackFrom(resource("resource"));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _))
      .WillOnce([](Envoy::Config::ConfigUpdateFailureReason, const EnvoyException* e) {
        EXPECT_TRUE(IsSubstring(
            "", "",
            "type URL type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment embedded "
            "in an individual Any does not match the message-wide type URL badtypeurl",
            e->what()));
      });
  EXPECT_CALL(*ttl_timer_, disableTimer());
  state_->handleResponse(response);
}

// Verifies shared `XdsSymbolTable` deduplication across `SotwSubscriptionState` instances and full
// reclamation of active symbols when tracked names are removed.
TEST_P(SotwSubscriptionStateTest, SharedSymbolTableCompressionAndReclamation) {
  auto symbol_table = XdsSymbolTable::create();
  const size_t initial_active = symbol_table->numActiveSymbols();
  const size_t initial_total = symbol_table->numSymbols();

  NiceMock<Event::MockDispatcher> local_dispatcher_a;
  NiceMock<Event::MockDispatcher> local_dispatcher_b;
  std::ignore = new NiceMock<Event::MockTimer>(&local_dispatcher_a);
  std::ignore = new NiceMock<Event::MockTimer>(&local_dispatcher_b);

  XdsMux::SotwSubscriptionStateFactory factory_a(local_dispatcher_a, makeOptRef(*symbol_table));
  XdsMux::SotwSubscriptionStateFactory factory_b(local_dispatcher_b, makeOptRef(*symbol_table));

  auto state_a = factory_a.makeSubscriptionState(
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(), callbacks_,
      resource_decoder_, XdsConfigTrackerOptRef(), *xds_resources_delegate_, "xds_server");
  auto state_b = factory_b.makeSubscriptionState(
      "type.googleapis.com/envoy.config.cluster.v3.Cluster", callbacks_, resource_decoder_,
      XdsConfigTrackerOptRef(), XdsResourcesDelegateOptRef(), "xds_server");

  const std::string res_a =
      "xdstp://shared_authority/envoy.config.endpoint.v3.ClusterLoadAssignment/shared_ns/svc_1";
  const std::string res_b =
      "xdstp://shared_authority/envoy.config.cluster.v3.Cluster/shared_ns/svc_1";

  // Including duplicate addition and removal of unseen resource name.
  state_a->updateSubscriptionInterest({res_a}, {"xdstp://unseen_authority/unseen_res"});
  state_a->updateSubscriptionInterest({res_a}, {});
  const size_t symbols_after_a = symbol_table->numSymbols();
  state_b->updateSubscriptionInterest({res_b}, {});

  if (GetParam()) {
    EXPECT_GT(symbol_table->numActiveSymbols(), initial_active);
    EXPECT_EQ(symbol_table->numSymbols() - symbols_after_a, 2);
  } else {
    EXPECT_EQ(symbol_table->numActiveSymbols(), initial_active);
    EXPECT_EQ(symbol_table->numSymbols(), initial_total);
  }

  auto req_a = state_a->getNextRequestAckless();
  EXPECT_THAT(req_a->resource_names(), UnorderedElementsAre(res_a));

  // Verify `handleEstablishmentFailure` materializes tracked names accurately for the delegate.
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  EXPECT_CALL(callbacks_,
              onConfigUpdate(testing::Matcher<const std::vector<DecodedResourcePtr>&>(SizeIs(1)),
                             std::string(RESOURCE_VERSION)));
  state_a->handleEstablishmentFailure();

  // Remove all tracked names; all active symbols must be reclaimed.
  state_a->updateSubscriptionInterest({}, {res_a});
  state_b->updateSubscriptionInterest({}, {res_b});
  EXPECT_EQ(symbol_table->numActiveSymbols(), initial_active);
}

} // namespace
} // namespace Config
} // namespace Envoy
