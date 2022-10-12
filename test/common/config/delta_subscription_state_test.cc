#include <chrono>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/delta_subscription_state.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_mux/delta_subscription_state.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::IsSubstring;
using testing::NiceMock;
using testing::Pair;
using testing::Throw;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

namespace Envoy {
namespace Config {
namespace {

const char TypeUrl[] = "type.googleapis.com/envoy.config.cluster.v3.Cluster";
enum class LegacyOrUnified { Legacy, Unified };
const auto WildcardStr = std::string(Wildcard);

Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
populateRepeatedResource(std::vector<std::pair<std::string, std::string>> items) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_to;
  for (const auto& item : items) {
    auto* resource = add_to.Add();
    resource->set_name(item.first);
    resource->set_version(item.second);
  }
  return add_to;
}

Protobuf::RepeatedPtrField<std::string> populateRepeatedString(std::vector<std::string> items) {
  Protobuf::RepeatedPtrField<std::string> add_to;
  for (const auto& item : items) {
    auto* str = add_to.Add();
    *str = item;
  }
  return add_to;
}

class DeltaSubscriptionStateTestBase : public testing::TestWithParam<LegacyOrUnified> {
protected:
  DeltaSubscriptionStateTestBase(const std::string& type_url, LegacyOrUnified legacy_or_unified)
      : should_use_unified_(legacy_or_unified == LegacyOrUnified::Unified) {
    ttl_timer_ = new Event::MockTimer(&dispatcher_);

    if (should_use_unified_) {
      state_ = std::make_unique<Envoy::Config::XdsMux::DeltaSubscriptionState>(type_url, callbacks_,
                                                                               dispatcher_);
    } else {
      state_ = std::make_unique<Envoy::Config::DeltaSubscriptionState>(type_url, callbacks_,
                                                                       local_info_, dispatcher_);
    }
  }

  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed) {
    if (should_use_unified_) {
      absl::get<1>(state_)->updateSubscriptionInterest(cur_added, cur_removed);
    } else {
      absl::get<0>(state_)->updateSubscriptionInterest(cur_added, cur_removed);
    }
  }

  std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryRequest> getNextRequestAckless() {
    if (should_use_unified_) {
      return absl::get<1>(state_)->getNextRequestAckless();
    }
    return std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryRequest>(
        absl::get<0>(state_)->getNextRequestAckless());
  }

  UpdateAck
  handleResponse(const envoy::service::discovery::v3::DeltaDiscoveryResponse& response_proto) {
    if (should_use_unified_) {
      return absl::get<1>(state_)->handleResponse(response_proto);
    }
    return absl::get<0>(state_)->handleResponse(response_proto);
  }

  UpdateAck deliverDiscoveryResponse(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info, absl::optional<std::string> nonce = absl::nullopt,
      bool expect_config_update_call = true, absl::optional<uint64_t> updated_resources = {}) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse message;
    *message.mutable_resources() = added_resources;
    *message.mutable_removed_resources() = removed_resources;
    message.set_system_version_info(version_info);
    if (nonce.has_value()) {
      message.set_nonce(nonce.value());
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _))
        .Times(expect_config_update_call ? 1 : 0)
        .WillRepeatedly(Invoke([updated_resources](const auto& added, const auto&, const auto&) {
          if (updated_resources) {
            EXPECT_EQ(added.size(), *updated_resources);
          }
        }));
    return handleResponse(message);
  }

  UpdateAck deliverBadDiscoveryResponse(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info, std::string nonce, std::string error_message) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse message;
    *message.mutable_resources() = added_resources;
    *message.mutable_removed_resources() = removed_resources;
    message.set_system_version_info(version_info);
    message.set_nonce(nonce);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _)).WillOnce(Throw(EnvoyException(error_message)));
    return handleResponse(message);
  }

  UpdateAck
  deliverSimpleDiscoveryResponse(std::vector<std::pair<std::string, std::string>> added_resources,
                                 std::vector<std::string> removed_resources,
                                 const std::string& version_info) {
    EXPECT_CALL(*ttl_timer_, disableTimer());
    auto add = populateRepeatedResource(added_resources);
    auto remove = populateRepeatedString(removed_resources);
    return deliverDiscoveryResponse(add, remove, version_info);
  }

  void markStreamFresh() {
    if (should_use_unified_) {
      absl::get<1>(state_)->markStreamFresh();
    } else {
      absl::get<0>(state_)->markStreamFresh();
    }
  }

  bool subscriptionUpdatePending() {
    if (should_use_unified_) {
      return absl::get<1>(state_)->subscriptionUpdatePending();
    }
    return absl::get<0>(state_)->subscriptionUpdatePending();
  }

  NiceMock<MockUntypedConfigUpdateCallbacks> callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* ttl_timer_;
  // We start out interested in three resources: name1, name2, and name3.
  absl::variant<std::unique_ptr<Envoy::Config::DeltaSubscriptionState>,
                std::unique_ptr<Envoy::Config::XdsMux::DeltaSubscriptionState>>
      state_;
  bool should_use_unified_;
};

class DeltaSubscriptionStateTestBlank : public DeltaSubscriptionStateTestBase {
public:
  DeltaSubscriptionStateTestBlank() : DeltaSubscriptionStateTestBase(TypeUrl, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(DeltaSubscriptionStateTestBlank, DeltaSubscriptionStateTestBlank,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

// Checks if subscriptionUpdatePending returns correct value depending on scenario.
TEST_P(DeltaSubscriptionStateTestBlank, SubscriptionPendingTest) {
  // We should send a request, because nothing has been sent out yet. Note that this means
  // subscribing to the wildcard resource.
  EXPECT_TRUE(subscriptionUpdatePending());
  getNextRequestAckless();

  // We should not be sending any requests if nothing yet changed since last time we sent a
  // request. Or if out subscription interest was not modified.
  EXPECT_FALSE(subscriptionUpdatePending());
  updateSubscriptionInterest({}, {});
  EXPECT_FALSE(subscriptionUpdatePending());

  // We should send a request, because our interest changed (we are interested in foo now).
  updateSubscriptionInterest({"foo"}, {});
  EXPECT_TRUE(subscriptionUpdatePending());
  getNextRequestAckless();

  // We should send a request after a new stream is established if we are interested in some
  // resource.
  EXPECT_FALSE(subscriptionUpdatePending());
  markStreamFresh();
  EXPECT_TRUE(subscriptionUpdatePending());
  getNextRequestAckless();

  // We should send a request, because our interest changed (we are not interested in foo and in
  // wildcard resource any more).
  EXPECT_FALSE(subscriptionUpdatePending());
  updateSubscriptionInterest({}, {WildcardStr, "foo"});
  EXPECT_TRUE(subscriptionUpdatePending());
  getNextRequestAckless();

  // We should not be sending anything after stream reestablishing, because we are not interested in
  // anything.
  markStreamFresh();
  EXPECT_FALSE(subscriptionUpdatePending());
}

// Check if requested resources are dropped from the cache immediately after losing interest in them
// in case we don't have a wildcard subscription. In such case there's no ambiguity whether a
// dropped resource could come from the wildcard subscription.
//
// Dropping from the cache can be seen through the initial_resource_versions field in the initial
// request.
TEST_P(DeltaSubscriptionStateTestBlank, ResourceTransitionNonWildcardFromRequestedToDropped) {
  updateSubscriptionInterest({"foo", "bar"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());

  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"bar", "1"}}, {}, "d1");
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("bar", "1")));

  updateSubscriptionInterest({}, {"foo"});
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_THAT(req->resource_names_unsubscribe(), UnorderedElementsAre("foo"));
  deliverSimpleDiscoveryResponse({}, {"foo"}, "d2");

  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(), UnorderedElementsAre(Pair("bar", "1")));
}

// Check if we keep foo resource in cache even if we lost interest in it. It could be a part of the
// wildcard subscription.
TEST_P(DeltaSubscriptionStateTestBlank, ResourceTransitionWithWildcardFromRequestedToAmbiguous) {
  // subscribe to foo and make sure we have it.
  updateSubscriptionInterest({WildcardStr, "foo", "bar"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"bar", "1"}, {"wild1", "1"}}, {}, "d1");

  // ensure that foo is a part of resource versions
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("bar", "1"), Pair("wild1", "1")));

  // unsubscribe from foo just before the stream breaks, make sure we still send the foo initial
  // version
  updateSubscriptionInterest({}, {"foo"});
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_THAT(req->resource_names_unsubscribe(), UnorderedElementsAre("foo"));
  // didn't receive a reply
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("bar", "1"), Pair("wild1", "1")));
}

// Check that foo and bar do not appear in initial versions after we lost interest. Foo won't
// appear, because we got a reply from server confirming dropping the resource. Bar won't appear
// because we never got a reply from server with a version of it.
TEST_P(DeltaSubscriptionStateTestBlank, ResourceTransitionWithWildcardFromRequestedToDropped) {
  // subscribe to foo and bar and make sure we have it.
  updateSubscriptionInterest({WildcardStr, "foo", "bar", "baz"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(),
              UnorderedElementsAre(WildcardStr, "foo", "bar", "baz"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"baz", "1"}, {"wild1", "1"}}, {}, "d1");

  // ensure that foo is a part of resource versions, bar won't be, because we don't have its version
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(),
              UnorderedElementsAre(WildcardStr, "foo", "bar", "baz"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("baz", "1"), Pair("wild1", "1")));

  // unsubscribe from foo and bar, and receive an confirmation about dropping foo. Now neither will
  // appear in initial versions in the initial request after breaking the stream.
  updateSubscriptionInterest({}, {"foo", "bar"});
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_THAT(req->resource_names_unsubscribe(), UnorderedElementsAre("foo", "bar"));
  deliverSimpleDiscoveryResponse({}, {"foo"}, "d2");
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "baz"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("baz", "1"), Pair("wild1", "1")));
}

// Check that we move the resource from wildcard subscription to requested without losing version
// information about it.
TEST_P(DeltaSubscriptionStateTestBlank, ResourceTransitionWithWildcardFromWildcardToRequested) {
  updateSubscriptionInterest({}, {});
  auto req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"wild1", "1"}}, {}, "d1");

  updateSubscriptionInterest({"foo"}, {});
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("wild1", "1")));
}

// Check that we move the ambiguous resource to requested without losing version information about
// it.
TEST_P(DeltaSubscriptionStateTestBlank, ResourceTransitionWithWildcardFromAmbiguousToRequested) {
  updateSubscriptionInterest({WildcardStr, "foo"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"wild1", "1"}}, {}, "d1");

  // make foo ambiguous and request it again
  updateSubscriptionInterest({}, {"foo"});
  updateSubscriptionInterest({"foo"}, {});
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("wild1", "1")));
}

// Check if we correctly decide to send a legacy wildcard initial request.
TEST_P(DeltaSubscriptionStateTestBlank, LegacyWildcardInitialRequests) {
  updateSubscriptionInterest({}, {});
  auto req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  deliverSimpleDiscoveryResponse({{"wild1", "1"}}, {}, "d1");

  // unsubscribing from unknown resource should keep the legacy
  // wildcard mode
  updateSubscriptionInterest({}, {"unknown"});
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());

  updateSubscriptionInterest({"foo"}, {});
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("foo"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}}, {}, "d1");
  updateSubscriptionInterest({}, {"foo"});
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_THAT(req->resource_names_unsubscribe(), UnorderedElementsAre("foo"));
  deliverSimpleDiscoveryResponse({}, {"foo"}, "d1");

  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
}

// Validate that the resources versions are updated and sent upon reconnection.
// Regression test of: https://github.com/envoyproxy/envoy/issues/20699
TEST_P(DeltaSubscriptionStateTestBlank, ReconnectResourcesVersions) {
  // Subscribe to foo and bar.
  updateSubscriptionInterest({WildcardStr, "foo", "bar"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  // Deliver foo, bar, and a wild with version 1.
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"bar", "1"}, {"wild", "1"}}, {}, "d1");

  // Update the versions of foo and wild to 2.
  deliverSimpleDiscoveryResponse({{"foo", "2"}, {"wild", "2"}}, {}, "d2");

  // Reconnect, and end validate the initial resources versions.
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "2"), Pair("bar", "1"), Pair("wild", "2")));
}

// Check that ambiguous resources may also receive a heartbeat message.
TEST_P(DeltaSubscriptionStateTestBlank, AmbiguousResourceTTL) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(std::chrono::milliseconds(0));

  auto create_resource_with_ttl = [](absl::string_view name, absl::string_view version,
                                     absl::optional<std::chrono::seconds> ttl_s,
                                     bool include_resource) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
    auto* resource = added_resources.Add();
    resource->set_name(std::string(name));
    resource->set_version(std::string(version));

    if (include_resource) {
      resource->mutable_resource();
    }

    if (ttl_s) {
      ProtobufWkt::Duration ttl;
      ttl.set_seconds(ttl_s->count());
      resource->mutable_ttl()->CopyFrom(ttl);
    }

    return added_resources;
  };

  updateSubscriptionInterest({WildcardStr, "foo"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre(WildcardStr, "foo"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  {
    EXPECT_CALL(*ttl_timer_, enabled());
    EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(1000), _));
    deliverDiscoveryResponse(create_resource_with_ttl("foo", "1", std::chrono::seconds(1), true),
                             {}, "debug1", "nonce1");
  }

  // make foo ambiguous
  updateSubscriptionInterest({}, {"foo"});
  req = getNextRequestAckless();
  EXPECT_TRUE(req->resource_names_subscribe().empty());
  EXPECT_THAT(req->resource_names_unsubscribe(), UnorderedElementsAre("foo"));
  {
    // Refresh the TTL with a heartbeat. The resource should not be passed to the update callbacks.
    EXPECT_CALL(*ttl_timer_, enabled());
    deliverDiscoveryResponse(create_resource_with_ttl("foo", "1", std::chrono::seconds(1), false),
                             {}, "debug1", "nonce1", true, 0);
  }

  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  time_system.setSystemTime(std::chrono::seconds(2));

  // Invoke the TTL.
  ttl_timer_->invokeCallback();
}

// Checks that we ignore resources that we haven't asked for.
TEST_P(DeltaSubscriptionStateTestBlank, IgnoreSuperfluousResources) {
  updateSubscriptionInterest({"foo", "bar"}, {});
  auto req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_TRUE(req->initial_resource_versions().empty());
  deliverSimpleDiscoveryResponse({{"foo", "1"}, {"bar", "1"}, {"did-not-want", "1"}, {"spam", "1"}},
                                 {}, "d1");

  // Force a reconnection and resending of the "initial" message. If the initial_resource_versions
  // in the message contains resources like did-not-want or spam, we haven't ignored that as we
  // should.
  markStreamFresh();
  req = getNextRequestAckless();
  EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("foo", "bar"));
  EXPECT_TRUE(req->resource_names_unsubscribe().empty());
  EXPECT_THAT(req->initial_resource_versions(),
              UnorderedElementsAre(Pair("foo", "1"), Pair("bar", "1")));
}

class DeltaSubscriptionStateTestWithResources : public DeltaSubscriptionStateTestBase {
protected:
  DeltaSubscriptionStateTestWithResources(
      const std::string& type_url, LegacyOrUnified legacy_or_unified,
      const absl::flat_hash_set<std::string> initial_resources = {"name1", "name2", "name3"})
      : DeltaSubscriptionStateTestBase(type_url, legacy_or_unified) {
    updateSubscriptionInterest(initial_resources, {});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                // UnorderedElementsAre("name1", "name2", "name3"));
                UnorderedElementsAreArray(initial_resources.cbegin(), initial_resources.cend()));
  }
};

class DeltaSubscriptionStateTest : public DeltaSubscriptionStateTestWithResources {
public:
  DeltaSubscriptionStateTest() : DeltaSubscriptionStateTestWithResources(TypeUrl, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(DeltaSubscriptionStateTest, DeltaSubscriptionStateTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

// Delta subscription state of a wildcard subscription request.
class WildcardDeltaSubscriptionStateTest : public DeltaSubscriptionStateTestWithResources {
public:
  WildcardDeltaSubscriptionStateTest()
      : DeltaSubscriptionStateTestWithResources(TypeUrl, GetParam(), {}) {}
};

INSTANTIATE_TEST_SUITE_P(WildcardDeltaSubscriptionStateTest, WildcardDeltaSubscriptionStateTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

// Basic gaining/losing interest in resources should lead to subscription updates.
TEST_P(DeltaSubscriptionStateTest, SubscribeAndUnsubscribe) {
  {
    updateSubscriptionInterest({"name4"}, {"name1"});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("name1"));
  }
  {
    updateSubscriptionInterest({"name1"}, {"name3", "name4"});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name1"));
    EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("name3", "name4"));
  }
}

// Resources has no subscriptions should not be tracked.
TEST_P(DeltaSubscriptionStateTest, NewPushDoesntAddUntrackedResources) {
  { // Add "name4", "name5", "name6" and remove "name1", "name2", "name3".
    updateSubscriptionInterest({"name4", "name5", "name6"}, {"name1", "name2", "name3"});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre("name4", "name5", "name6"));
    EXPECT_THAT(cur_request->resource_names_unsubscribe(),
                UnorderedElementsAre("name1", "name2", "name3"));
  }
  {
    // On Reconnection, only "name4", "name5", "name6" are sent.
    markStreamFresh();
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre("name4", "name5", "name6"));
    EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
    EXPECT_TRUE(cur_request->initial_resource_versions().empty());
  }
  // The xDS server's first response includes removed items name1 and 2, and a
  // completely unrelated resource "bluhbluh".
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"},
                                  {"bluhbluh", "bluh"},
                                  {"name6", "version6A"},
                                  {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  { // Simulate a stream reconnection, just to see the current resource_state_.
    markStreamFresh();
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre("name4", "name5", "name6"));
    EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
    ASSERT_EQ(cur_request->initial_resource_versions().size(), 1);
    EXPECT_TRUE(cur_request->initial_resource_versions().contains("name6"));
    EXPECT_EQ(cur_request->initial_resource_versions().at("name6"), "version6A");
  }
}

// Delta xDS reliably queues up and sends all discovery requests, even in situations where it isn't
// strictly necessary. E.g.: if you subscribe but then unsubscribe to a given resource, all before a
// request was able to be sent, two requests will be sent. The following tests demonstrate this.
//
// If Envoy decided it wasn't interested in a resource and then (before a request was sent) decided
// it was again, for all we know, it dropped that resource in between and needs to retrieve it
// again. So, we *should* send a request "re-"subscribing. This means that the server needs to
// interpret the resource_names_subscribe field as "send these resources even if you think Envoy
// already has them".
TEST_P(DeltaSubscriptionStateTest, RemoveThenAdd) {
  updateSubscriptionInterest({}, {"name3"});
  updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name3"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Due to how our implementation provides the required behavior tested in RemoveThenAdd, the
// add-then-remove case *also* causes the resource to be referred to in the request (as an
// unsubscribe).
// Unlike the remove-then-add case, this one really is unnecessary, and ideally we would have
// the request simply not include any mention of the resource. Oh well.
// This test is just here to illustrate that this behavior exists, not to enforce that it
// should be like this. What *is* important: the server must happily and cleanly ignore
// "unsubscribe from [resource name I have never before referred to]" requests.
TEST_P(DeltaSubscriptionStateTest, AddThenRemove) {
  updateSubscriptionInterest({"name4"}, {});
  updateSubscriptionInterest({}, {"name4"});
  auto cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("name4"));
}

// add/remove/add == add.
TEST_P(DeltaSubscriptionStateTest, AddRemoveAdd) {
  updateSubscriptionInterest({"name4"}, {});
  updateSubscriptionInterest({}, {"name4"});
  updateSubscriptionInterest({"name4"}, {});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// remove/add/remove == remove.
TEST_P(DeltaSubscriptionStateTest, RemoveAddRemove) {
  updateSubscriptionInterest({}, {"name3"});
  updateSubscriptionInterest({"name3"}, {});
  updateSubscriptionInterest({}, {"name3"});
  auto cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("name3"));
}

// Starts with 1,2,3. 4 is added/removed/added. In those same updates, 1,2,3 are
// removed/added/removed. End result should be 4 added and 1,2,3 removed.
TEST_P(DeltaSubscriptionStateTest, BothAddAndRemove) {
  updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  updateSubscriptionInterest({"name1", "name2", "name3"}, {"name4"});
  updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
  EXPECT_THAT(cur_request->resource_names_unsubscribe(),
              UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_P(DeltaSubscriptionStateTest, CumulativeUpdates) {
  updateSubscriptionInterest({"name4"}, {});
  updateSubscriptionInterest({"name5"}, {});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4", "name5"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Verifies that a sequence of good and bad responses from the server all get the appropriate
// ACKs/NACKs from Envoy.
TEST_P(DeltaSubscriptionStateTest, AckGenerated) {
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response updates 1 and 2, and adds 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1B"}, {"name2", "version2B"}, {"name3", "version3A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug2", "nonce2");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update all 3, and so should produce a NACK.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1C"}, {"name2", "version2C"}, {"name3", "version3B"}});
    EXPECT_CALL(*ttl_timer_, disableTimer()).Times(0);
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug3", "nonce3", "oh no");
    EXPECT_EQ("nonce3", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The last response successfully updates all 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3C"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug4", "nonce4");
    EXPECT_EQ("nonce4", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // Bad response error detail is truncated if it's too large.
  {
    const std::string very_large_error_message(1 << 20, 'A');
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3D"}});
    EXPECT_CALL(*ttl_timer_, disableTimer()).Times(0);
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug5", "nonce5",
                                                very_large_error_message);
    EXPECT_EQ("nonce5", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
    EXPECT_TRUE(absl::EndsWith(ack.error_detail_.message(), "AAAAAAA...(truncated)"));
    EXPECT_LT(ack.error_detail_.message().length(), very_large_error_message.length());
  }
}

// Verifies that a sequence of good and bad responses from the server all get the appropriate
// ACKs/NACKs from Envoy when the state is updated before the resources are ingested
// (`envoy.reloadable_features.delta_xds_subscription_state_tracking_fix` is false).
// TODO(adisuissa): This test should be removed once the runtime flag
// `envoy.reloadable_features.delta_xds_subscription_state_tracking_fix` is
// removed and the AckGenerated test will cover the correct behavior.
TEST_P(DeltaSubscriptionStateTest, AckGeneratedPreStateFix) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.delta_xds_subscription_state_tracking_fix", "false"}});
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response updates 1 and 2, and adds 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1B"}, {"name2", "version2B"}, {"name3", "version3A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug2", "nonce2");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update all 3, and so should produce a NACK.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1C"}, {"name2", "version2C"}, {"name3", "version3B"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug3", "nonce3", "oh no");
    EXPECT_EQ("nonce3", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The last response successfully updates all 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3C"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug4", "nonce4");
    EXPECT_EQ("nonce4", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // Bad response error detail is truncated if it's too large.
  {
    const std::string very_large_error_message(1 << 20, 'A');
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3D"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug5", "nonce5",
                                                very_large_error_message);
    EXPECT_EQ("nonce5", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
    EXPECT_TRUE(absl::EndsWith(ack.error_detail_.message(), "AAAAAAA...(truncated)"));
    EXPECT_LT(ack.error_detail_.message().length(), very_large_error_message.length());
  }
}

// Tests population of the initial_resource_versions map in the first request of a new stream.
// Tests that
// 1) resources we have a version of are present in the map,
// 2) resources we are interested in but don't have are not present, and
// 3) resources we have lost interest in are not present.
TEST_P(DeltaSubscriptionStateTest, ResourceGoneLeadsToBlankInitialVersion) {
  {
    // The xDS server's first update includes items for name1 and 2, but not 3.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    deliverDiscoveryResponse(add1_2, {}, "debugversion1");
    markStreamFresh(); // simulate a stream reconnection
    auto cur_request = getNextRequestAckless();
    EXPECT_EQ("version1A", cur_request->initial_resource_versions().at("name1"));
    EXPECT_EQ("version2A", cur_request->initial_resource_versions().at("name2"));
    EXPECT_EQ(cur_request->initial_resource_versions().end(),
              cur_request->initial_resource_versions().find("name3"));
  }

  {
    // The next update updates 1, removes 2, and adds 3. The map should then have 1 and 3.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_3 =
        populateRepeatedResource({{"name1", "version1B"}, {"name3", "version3A"}});
    Protobuf::RepeatedPtrField<std::string> remove2;
    *remove2.Add() = "name2";
    EXPECT_CALL(*ttl_timer_, disableTimer()).Times(2);
    deliverDiscoveryResponse(add1_3, remove2, "debugversion2");
    markStreamFresh(); // simulate a stream reconnection
    auto cur_request = getNextRequestAckless();
    EXPECT_EQ("version1B", cur_request->initial_resource_versions().at("name1"));
    EXPECT_EQ(cur_request->initial_resource_versions().end(),
              cur_request->initial_resource_versions().find("name2"));
    EXPECT_EQ("version3A", cur_request->initial_resource_versions().at("name3"));
  }

  {
    // The next update removes 1 and 3. The map we send the server should be empty...
    Protobuf::RepeatedPtrField<std::string> remove1_3;
    *remove1_3.Add() = "name1";
    *remove1_3.Add() = "name3";
    deliverDiscoveryResponse({}, remove1_3, "debugversion3");
    markStreamFresh(); // simulate a stream reconnection
    auto cur_request = getNextRequestAckless();
    EXPECT_TRUE(cur_request->initial_resource_versions().empty());
  }

  {
    // ...but our own map should remember our interest. In particular, losing interest in a
    // resource should cause its name to appear in the next request's resource_names_unsubscribe.
    updateSubscriptionInterest({"name4"}, {"name1", "name2"});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("name1", "name2"));
  }
}

// For non-wildcard subscription, upon a reconnection, the server is supposed to assume a
// blank slate for the Envoy's state (hence the need for initial_resource_versions).
// The resource_names_subscribe of the first message must therefore be every resource the
// Envoy is interested in.
//
// resource_names_unsubscribe, on the other hand, is always blank in the first request - even if,
// in between the last request of the last stream and the first request of the new stream, Envoy
// lost interest in a resource. The unsubscription implicitly takes effect by simply saying
// nothing about the resource in the newly reconnected stream.
TEST_P(DeltaSubscriptionStateTest, SubscribeAndUnsubscribeAfterReconnect) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  updateSubscriptionInterest({"name4"}, {"name1"});
  markStreamFresh(); // simulate a stream reconnection
  auto cur_request = getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: we lost interest.
  // name2: yes do include: we are interested, its non-wildcard, and we have a version of it.
  // name3: yes do include: even though we don't have a version of it, we are interested.
  // name4: yes do include: we are newly interested. (If this wasn't a stream reconnect, only
  //        name4 would belong in this subscribe field).
  EXPECT_THAT(cur_request->resource_names_subscribe(),
              UnorderedElementsAre("name2", "name3", "name4"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Check that switching into wildcard subscription after initial
// request switches us into the explicit wildcard mode.
TEST_P(DeltaSubscriptionStateTest, SwitchIntoWildcardMode) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  // We call deliverDiscoveryResponse twice in this test.
  EXPECT_CALL(*ttl_timer_, disableTimer()).Times(2);
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  // switch into wildcard mode
  updateSubscriptionInterest({"name4", WildcardStr}, {"name1"});
  markStreamFresh(); // simulate a stream reconnection
  auto cur_request = getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: we lost interest.
  // name2: yes do include: we are explicitly interested (from test's base constructor)
  // name3: yes do include: we are explicitly interested (from test's base constructor)
  // name4: yes do include: we are explicitly interested
  // *: explicit wildcard subscription
  EXPECT_THAT(cur_request->resource_names_subscribe(),
              UnorderedElementsAre("name2", "name3", "name4", Wildcard));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add4_5 =
      populateRepeatedResource({{"name4", "version4A"}, {"name5", "version5A"}});
  deliverDiscoveryResponse(add4_5, {}, "debugversion1");

  markStreamFresh(); // simulate a stream reconnection
  cur_request = getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: we lost interest.
  // name2: yes do include: we are explicitly interested (from test's base constructor)
  // name3: yes do include: we are explicitly interested (from test's base constructor)
  // name4: yes do include: we are explicitly interested
  // name5: do not include: we are implicitly interested, so this resource should not appear on the
  // initial request
  // *: explicit wildcard subscription
  EXPECT_THAT(cur_request->resource_names_subscribe(),
              UnorderedElementsAre("name2", "name3", "name4", Wildcard));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// For wildcard subscription, upon a reconnection, the server is supposed to assume a blank slate
// for the Envoy's state (hence the need for initial_resource_versions), and the
// resource_names_subscribe and resource_names_unsubscribe must be empty if we haven't gained any
// new explicit interest in a resource. In such case, the client should send an empty request.
TEST_P(WildcardDeltaSubscriptionStateTest, SubscribeAndUnsubscribeAfterReconnectImplicit) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  markStreamFresh(); // simulate a stream reconnection
  auto cur_request = getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: we lost interest.
  // name2: do not include: we are implicitly interested, but for wildcard it shouldn't be provided.
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// For wildcard subscription, upon a reconnection, the server is supposed to assume a blank slate
// for the Envoy's state (hence the need for initial_resource_versions). The
// resource_names_unsubscribe must be empty (as is expected of every wildcard first message). The
// resource_names_subscribe should contain all the resources we are explicitly interested in and a
// special resource denoting a wildcard subscription.
TEST_P(WildcardDeltaSubscriptionStateTest, SubscribeAndUnsubscribeAfterReconnectExplicit) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  updateSubscriptionInterest({"name3"}, {});
  markStreamFresh(); // simulate a stream reconnection
  auto cur_request = getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: see below
  // name2: do not include: we are implicitly interested, but for wildcard it shouldn't be provided.
  // name3: yes do include: we are explicitly interested.
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre(Wildcard, "name3"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Check the contents of the requests after cancelling the wildcard
// subscription and then reconnection. The second request should look
// like a non-wildcard request, so mention all the known resources in
// the initial request.
TEST_P(WildcardDeltaSubscriptionStateTest, CancellingImplicitWildcardSubscription) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  updateSubscriptionInterest({"name3"}, {WildcardStr});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name3"));
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre(Wildcard));
  markStreamFresh(); // simulate a stream reconnection
  // Regarding the resource_names_subscribe field:
  // name1: do not include, see below
  // name2: do not include: it came from wildcard subscription we lost interest in, so we are not
  //        interested in name2 too
  // name3: yes do include: we are interested
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name3"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Check the contents of the requests after cancelling the wildcard
// subscription and then reconnection. The second request should look
// like a non-wildcard request, so mention all the known resources in
// the initial request.
TEST_P(WildcardDeltaSubscriptionStateTest, CancellingExplicitWildcardSubscription) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");
  // switch to explicit wildcard subscription
  updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name3"));

  // cancel wildcard subscription
  updateSubscriptionInterest({"name4"}, {WildcardStr});
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre(Wildcard));
  markStreamFresh(); // simulate a stream reconnection
  // Regarding the resource_names_subscribe field:
  // name1: do not include: see name2
  // name2: do not include: it came as a part of wildcard subscription we cancelled, so we are not
  // interested in this resource name3: yes do include: we are interested, and it's not wildcard.
  // name4: yes do include: we are interested, and it's not wildcard.
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name3", "name4"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Check that resource changes from being interested in implicitly to explicitly when we update the
// subscription interest. Such resources will show up in the initial wildcard requests
// too. Receiving the update on such resource will not change their interest mode.
TEST_P(WildcardDeltaSubscriptionStateTest, ExplicitInterestOverridesImplicit) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2_a =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  EXPECT_CALL(*ttl_timer_, disableTimer()).Times(2);
  deliverDiscoveryResponse(add1_2_a, {}, "debugversion1");

  // verify that neither name1 nor name2 appears in the initial request (they are of implicit
  // interest and initial wildcard request should not contain those).
  markStreamFresh(); // simulate a stream reconnection
  auto cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());

  // express the interest in name1 explicitly and verify that the follow-up request will contain it
  // (this also switches the wildcard mode to explicit, but we won't see * in resource names,
  // because we already are in wildcard mode).
  updateSubscriptionInterest({"name1"}, {});
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name1"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());

  // verify that name1 and * appear in the initial request (name1 is of explicit interest and we are
  // in explicit wildcard mode).
  markStreamFresh(); // simulate a stream reconnection
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name1", Wildcard));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());

  // verify that getting an update on name1 will keep name1 in the explicit interest mode
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2_b =
      populateRepeatedResource({{"name1", "version1B"}, {"name2", "version2B"}});
  deliverDiscoveryResponse(add1_2_b, {}, "debugversion1");
  markStreamFresh(); // simulate a stream reconnection
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("name1", Wildcard));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// Check that resource changes from being interested in implicitly to explicitly when we update the
// subscription interest. Such resources will show up in the initial wildcard requests
// too. Receiving the update on such resource will not change their interest mode.
TEST_P(WildcardDeltaSubscriptionStateTest, ResetToLegacyWildcardBehaviorOnStreamReset) {
  // verify that we will send the legacy wildcard subscription request
  // after stream reset
  updateSubscriptionInterest({"resource"}, {});
  auto cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("resource"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
  updateSubscriptionInterest({}, {"resource"});
  cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("resource"));
  markStreamFresh(); // simulate a stream reconnection
  cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());

  // verify that we will send the legacy wildcard subscription request
  // after stream reset and confirming our subscription interest
  updateSubscriptionInterest({"resource"}, {});
  cur_request = getNextRequestAckless();
  EXPECT_THAT(cur_request->resource_names_subscribe(), UnorderedElementsAre("resource"));
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
  updateSubscriptionInterest({}, {"resource"});
  cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_THAT(cur_request->resource_names_unsubscribe(), UnorderedElementsAre("resource"));
  markStreamFresh(); // simulate a stream reconnection
  updateSubscriptionInterest({}, {});
  cur_request = getNextRequestAckless();
  EXPECT_TRUE(cur_request->resource_names_subscribe().empty());
  EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
}

// All resources from the server should be tracked.
TEST_P(WildcardDeltaSubscriptionStateTest, AllResourcesFromServerAreTrackedInWildcardXDS) {
  { // Add "name4", "name5", "name6"
    updateSubscriptionInterest({"name4", "name5", "name6"}, {});
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre("name4", "name5", "name6"));
    EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
  }
  {
    // On Reconnection, only "name4", "name5", "name6" and wildcard resource are sent.
    markStreamFresh();
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre(WildcardStr, "name4", "name5", "name6"));
    EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
    EXPECT_TRUE(cur_request->initial_resource_versions().empty());
  }
  // The xDS server's first response includes removed items name1 and 2, and a
  // completely unrelated resource "bluhbluh".
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"},
                                  {"bluhbluh", "bluh"},
                                  {"name6", "version6A"},
                                  {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  { // Simulate a stream reconnection, just to see the current resource_state_.
    markStreamFresh();
    auto cur_request = getNextRequestAckless();
    EXPECT_THAT(cur_request->resource_names_subscribe(),
                UnorderedElementsAre(WildcardStr, "name4", "name5", "name6"));
    EXPECT_TRUE(cur_request->resource_names_unsubscribe().empty());
    ASSERT_EQ(cur_request->initial_resource_versions().size(), 4);
    EXPECT_EQ(cur_request->initial_resource_versions().at("name1"), "version1A");
    EXPECT_EQ(cur_request->initial_resource_versions().at("bluhbluh"), "bluh");
    EXPECT_EQ(cur_request->initial_resource_versions().at("name6"), "version6A");
    EXPECT_EQ(cur_request->initial_resource_versions().at("name2"), "version2A");
  }
}

// initial_resource_versions should not be present on messages after the first in a stream.
TEST_P(DeltaSubscriptionStateTest, InitialVersionMapFirstMessageOnly) {
  // First, verify that the first message of a new stream sends initial versions.
  {
    // The xDS server's first update gives us all three resources.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_all =
        populateRepeatedResource(
            {{"name1", "version1A"}, {"name2", "version2A"}, {"name3", "version3A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    deliverDiscoveryResponse(add_all, {}, "debugversion1");
    markStreamFresh(); // simulate a stream reconnection
    auto cur_request = getNextRequestAckless();
    EXPECT_EQ("version1A", cur_request->initial_resource_versions().at("name1"));
    EXPECT_EQ("version2A", cur_request->initial_resource_versions().at("name2"));
    EXPECT_EQ("version3A", cur_request->initial_resource_versions().at("name3"));
  }
  // Then, after updating the resources but not reconnecting the stream, verify that initial
  // versions are not sent.
  {
    updateSubscriptionInterest({"name4"}, {});
    // The xDS server updates our resources, and gives us our newly requested one too.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_all =
        populateRepeatedResource({{"name1", "version1B"},
                                  {"name2", "version2B"},
                                  {"name3", "version3B"},
                                  {"name4", "version4A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    deliverDiscoveryResponse(add_all, {}, "debugversion2");
    auto cur_request = getNextRequestAckless();
    EXPECT_TRUE(cur_request->initial_resource_versions().empty());
  }
}

TEST_P(DeltaSubscriptionStateTest, CheckUpdatePending) {
  // Note that the test fixture ctor causes the first request to be "sent", so we start in the
  // middle of a stream, with our initially interested resources having been requested already.
  EXPECT_FALSE(subscriptionUpdatePending());
  updateSubscriptionInterest({}, {}); // no change
  EXPECT_FALSE(subscriptionUpdatePending());
  markStreamFresh();
  EXPECT_TRUE(subscriptionUpdatePending());  // no change, BUT fresh stream
  updateSubscriptionInterest({}, {"name3"}); // one removed
  EXPECT_TRUE(subscriptionUpdatePending());
  updateSubscriptionInterest({"name3"}, {}); // one added
  EXPECT_TRUE(subscriptionUpdatePending());
}

// The next three tests test that duplicate resource names (whether additions or removals) cause
// DeltaSubscriptionState to reject the update without even trying to hand it to the consuming
// API's onConfigUpdate().
TEST_P(DeltaSubscriptionStateTest, DuplicatedAdd) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> additions =
      populateRepeatedResource({{"name1", "version1A"}, {"name1", "sdfsdfsdfds"}});
  UpdateAck ack = deliverDiscoveryResponse(additions, {}, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found among added/updated resources",
            ack.error_detail_.message());
}

TEST_P(DeltaSubscriptionStateTest, DuplicatedRemove) {
  Protobuf::RepeatedPtrField<std::string> removals;
  *removals.Add() = "name1";
  *removals.Add() = "name1";
  UpdateAck ack = deliverDiscoveryResponse({}, removals, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found in the union of added+removed resources",
            ack.error_detail_.message());
}

TEST_P(DeltaSubscriptionStateTest, AddedAndRemoved) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> additions =
      populateRepeatedResource({{"name1", "version1A"}});
  Protobuf::RepeatedPtrField<std::string> removals;
  *removals.Add() = "name1";
  UpdateAck ack =
      deliverDiscoveryResponse(additions, removals, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found in the union of added+removed resources",
            ack.error_detail_.message());
}

TEST_P(DeltaSubscriptionStateTest, ResourceTTL) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(std::chrono::milliseconds(0));

  auto create_resource_with_ttl = [](absl::optional<std::chrono::seconds> ttl_s,
                                     bool include_resource) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
    auto* resource = added_resources.Add();
    resource->set_name("name1");
    resource->set_version("version1A");

    if (include_resource) {
      resource->mutable_resource();
    }

    if (ttl_s) {
      ProtobufWkt::Duration ttl;
      ttl.set_seconds(ttl_s->count());
      resource->mutable_ttl()->CopyFrom(ttl);
    }

    return added_resources;
  };

  {
    EXPECT_CALL(*ttl_timer_, enabled());
    EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(1000), _));
    deliverDiscoveryResponse(create_resource_with_ttl(std::chrono::seconds(1), true), {}, "debug1",
                             "nonce1");
  }

  {
    // Increase the TTL.
    EXPECT_CALL(*ttl_timer_, enabled());
    EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(2000), _));
    deliverDiscoveryResponse(create_resource_with_ttl(std::chrono::seconds(2), true), {}, "debug1",
                             "nonce1", true, 1);
  }

  {
    // Refresh the TTL with a heartbeat. The resource should not be passed to the update callbacks.
    EXPECT_CALL(*ttl_timer_, enabled());
    deliverDiscoveryResponse(create_resource_with_ttl(std::chrono::seconds(2), false), {}, "debug1",
                             "nonce1", true, 0);
  }

  // Remove the TTL.
  EXPECT_CALL(*ttl_timer_, disableTimer());
  deliverDiscoveryResponse(create_resource_with_ttl(absl::nullopt, true), {}, "debug1", "nonce1",
                           true, 1);

  // Add back the TTL.
  EXPECT_CALL(*ttl_timer_, enabled());
  EXPECT_CALL(*ttl_timer_, enableTimer(_, _));
  deliverDiscoveryResponse(create_resource_with_ttl(std::chrono::seconds(2), true), {}, "debug1",
                           "nonce1");

  EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _));
  EXPECT_CALL(*ttl_timer_, disableTimer());
  time_system.setSystemTime(std::chrono::seconds(2));

  // Invoke the TTL.
  ttl_timer_->invokeCallback();
}

TEST_P(DeltaSubscriptionStateTest, TypeUrlMismatch) {
  envoy::service::discovery::v3::DeltaDiscoveryResponse message;

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> additions;
  auto* resource = additions.Add();
  resource->set_name("name1");
  resource->set_version("version1");
  resource->mutable_resource()->set_type_url("foo");

  *message.mutable_resources() = additions;
  *message.mutable_removed_resources() = {};
  message.set_system_version_info("version1");
  message.set_nonce("nonce1");
  message.set_type_url("bar");

  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _))
      .WillOnce(Invoke([](Envoy::Config::ConfigUpdateFailureReason, const EnvoyException* e) {
        EXPECT_TRUE(IsSubstring("", "",
                                "type URL foo embedded in an individual Any does not match the "
                                "message-wide type URL bar",
                                e->what()));
      }));
  handleResponse(message);
}

// Verifies that an update that is NACKed doesn't update the tracked
// versions of the registered resources.
TEST_P(DeltaSubscriptionStateTest, NoVersionUpdateOnNack) {
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    EXPECT_CALL(*ttl_timer_, disableTimer());
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update the 2 and add name3, and so should produce a NACK.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1B"}, {"name2", "version2B"}, {"name3", "version3A"}});
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.delta_xds_subscription_state_tracking_fix")) {
      EXPECT_CALL(*ttl_timer_, disableTimer());
    }
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug2", "nonce2", "oh no");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // Verify that a reconnect keeps the old versions.
  markStreamFresh();
  {
    auto req = getNextRequestAckless();
    EXPECT_THAT(req->resource_names_subscribe(), UnorderedElementsAre("name1", "name2", "name3"));
    EXPECT_TRUE(req->resource_names_unsubscribe().empty());
    EXPECT_THAT(req->initial_resource_versions(),
                UnorderedElementsAre(Pair("name1", "version1A"), Pair("name2", "version2A")));
  }
}

class VhdsDeltaSubscriptionStateTest : public DeltaSubscriptionStateTestWithResources {
public:
  VhdsDeltaSubscriptionStateTest()
      : DeltaSubscriptionStateTestWithResources("envoy.config.route.v3.VirtualHost", GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(VhdsDeltaSubscriptionStateTest, VhdsDeltaSubscriptionStateTest,
                         testing::ValuesIn({LegacyOrUnified::Legacy, LegacyOrUnified::Unified}));

TEST_P(VhdsDeltaSubscriptionStateTest, ResourceTTL) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(std::chrono::milliseconds(0));

  TestScopedRuntime scoped_runtime;

  auto create_resource_with_ttl = [](bool include_resource) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
    auto* resource = added_resources.Add();
    resource->set_name("name1");
    resource->set_version("version1A");

    if (include_resource) {
      resource->mutable_resource();
    }

    ProtobufWkt::Duration ttl;
    ttl.set_seconds(1);
    resource->mutable_ttl()->CopyFrom(ttl);

    return added_resources;
  };

  EXPECT_CALL(*ttl_timer_, enabled());
  EXPECT_CALL(*ttl_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  deliverDiscoveryResponse(create_resource_with_ttl(true), {}, "debug1", "nonce1", true, 1);

  // Heartbeat update should not be propagated to the subscription callback.
  EXPECT_CALL(*ttl_timer_, enabled());
  deliverDiscoveryResponse(create_resource_with_ttl(false), {}, "debug1", "nonce1", true, 1);
}

} // namespace
} // namespace Config
} // namespace Envoy
