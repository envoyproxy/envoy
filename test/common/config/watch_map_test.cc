#include <memory>

#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "common/config/watch_map.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace Envoy {
namespace Config {
namespace {

// expectDeltaAndSotwUpdate() EXPECTs two birds with one function call: we want to cover both SotW
// and delta, which, while mechanically different, can behave identically for our testing purposes.
// Specifically, as a simplification for these tests, every still-present resource is updated in
// every update. Therefore, a resource can never show up in the SotW update but not the delta
// update. We can therefore use the same expected_resources for both.
void expectDeltaAndSotwUpdate(
    MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>& callbacks,
    std::vector<envoy::api::v2::ClusterLoadAssignment> expected_resources,
    std::vector<std::string> expected_removals, const std::string& version) {
  EXPECT_CALL(callbacks, onConfigUpdate(_, version))
      .WillOnce(Invoke(
          [expected_resources](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& gotten_resources,
                               const std::string&) {
            EXPECT_EQ(expected_resources.size(), gotten_resources.size());
            for (size_t i = 0; i < expected_resources.size(); i++) {
              envoy::api::v2::ClusterLoadAssignment cur_gotten_resource;
              gotten_resources[i].UnpackTo(&cur_gotten_resource);
              EXPECT_TRUE(TestUtility::protoEqual(cur_gotten_resource, expected_resources[i]));
            }
          }));
  EXPECT_CALL(callbacks, onConfigUpdate(_, _, _))
      .WillOnce(
          Invoke([expected_resources, expected_removals, version](
                     const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& gotten_resources,
                     const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                     const std::string&) {
            EXPECT_EQ(expected_resources.size(), gotten_resources.size());
            for (size_t i = 0; i < expected_resources.size(); i++) {
              EXPECT_EQ(gotten_resources[i].version(), version);
              envoy::api::v2::ClusterLoadAssignment cur_gotten_resource;
              gotten_resources[i].resource().UnpackTo(&cur_gotten_resource);
              EXPECT_TRUE(TestUtility::protoEqual(cur_gotten_resource, expected_resources[i]));
            }
            EXPECT_EQ(expected_removals.size(), removed_resources.size());
            for (size_t i = 0; i < expected_removals.size(); i++) {
              EXPECT_EQ(expected_removals[i], removed_resources[i]);
            }
          }));
}

Protobuf::RepeatedPtrField<envoy::api::v2::Resource>
wrapInResource(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& anys,
               const std::string& version) {
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> ret;
  for (const auto& a : anys) {
    envoy::api::v2::ClusterLoadAssignment cur_endpoint;
    a.UnpackTo(&cur_endpoint);
    auto* cur_resource = ret.Add();
    cur_resource->set_name(cur_endpoint.cluster_name());
    cur_resource->mutable_resource()->CopyFrom(a);
    cur_resource->set_version(version);
  }
  return ret;
}

// Similar to expectDeltaAndSotwUpdate(), but making the onConfigUpdate() happen, rather than
// EXPECTing it.
void doDeltaAndSotwUpdate(WatchMap& watch_map,
                          Protobuf::RepeatedPtrField<ProtobufWkt::Any> sotw_resources,
                          std::vector<std::string> removed_names, std::string version) {
  watch_map.onConfigUpdate(sotw_resources, version);

  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> delta_resources =
      wrapInResource(sotw_resources, version);
  Protobuf::RepeatedPtrField<std::string> removed_names_proto;
  for (auto n : removed_names) {
    *removed_names_proto.Add() = n;
  }
  watch_map.onConfigUpdate(delta_resources, removed_names_proto, "version1");
}

// Tests the simple case of a single watch. Checks that the watch will not be told of updates to
// resources it doesn't care about. Checks that the watch can later decide it does care about them,
// and then receive subsequent updates to them.
TEST(WatchMapTest, Basic) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks;
  WatchMap watch_map;
  WatchMap::Token token = watch_map.addWatch(callbacks);

  {
    // The watch is interested in Alice and Bob...
    std::set<std::string> update_to({"alice", "bob"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token, update_to);
    EXPECT_EQ(update_to, added_removed.first);
    EXPECT_TRUE(added_removed.second.empty());

    // ...the update is going to contain Bob and Carol...
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    envoy::api::v2::ClusterLoadAssignment bob;
    bob.set_cluster_name("bob");
    updated_resources.Add()->PackFrom(bob);
    envoy::api::v2::ClusterLoadAssignment carol;
    carol.set_cluster_name("carol");
    updated_resources.Add()->PackFrom(carol);

    // ...so the watch should receive only Bob.
    std::vector<envoy::api::v2::ClusterLoadAssignment> expected_resources;
    expected_resources.push_back(bob);

    expectDeltaAndSotwUpdate(callbacks, expected_resources, {}, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }

  {
    // The watch is now interested in Bob, Carol, Dave, Eve...
    std::set<std::string> update_to({"bob", "carol", "dave", "eve"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token, update_to);
    EXPECT_EQ(std::set<std::string>({"carol", "dave", "eve"}), added_removed.first);
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.second);

    // ...the update is going to contain Alice, Carol, Dave...
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    envoy::api::v2::ClusterLoadAssignment alice;
    alice.set_cluster_name("alice");
    updated_resources.Add()->PackFrom(alice);
    envoy::api::v2::ClusterLoadAssignment carol;
    carol.set_cluster_name("carol");
    updated_resources.Add()->PackFrom(carol);
    envoy::api::v2::ClusterLoadAssignment dave;
    dave.set_cluster_name("dave");
    updated_resources.Add()->PackFrom(dave);

    // ...so the watch should receive only Carol and Dave.
    std::vector<envoy::api::v2::ClusterLoadAssignment> expected_resources;
    expected_resources.push_back(carol);
    expected_resources.push_back(dave);

    expectDeltaAndSotwUpdate(callbacks, expected_resources, {"bob"}, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {"bob"}, "version2");
  }

  // Clean removal of the watch: first update to "interested in nothing", then remove.
  std::pair<std::set<std::string>, std::set<std::string>> added_removed =
      watch_map.updateWatchInterest(token, {});
  EXPECT_EQ(std::set<std::string>({"bob", "carol", "dave", "eve"}), added_removed.second);
  EXPECT_TRUE(watch_map.removeWatch(token));
}

// Checks the following:
// First watch on a resource name ==> updateWatchInterest() returns "add it to subscription"
// Second watch on that name ==> updateWatchInterest() returns nothing about that name
// Original watch loses interest ==> nothing
// Second watch also loses interest ==> "remove it from subscription"
TEST(WatchMapTest, Overlap) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks1;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks2;
  WatchMap watch_map;
  WatchMap::Token token1 = watch_map.addWatch(callbacks1);
  WatchMap::Token token2 = watch_map.addWatch(callbacks2);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
  envoy::api::v2::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  updated_resources.Add()->PackFrom(alice);

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // First watch receives update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token2, update_to);
    EXPECT_TRUE(added_removed.first.empty()); // nothing happens
    EXPECT_TRUE(added_removed.second.empty());

    // Both watches receive update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version2");
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version2");
  }
  // First watch loses interest.
  {
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, {});
    EXPECT_TRUE(added_removed.first.empty()); // nothing happens
    EXPECT_TRUE(added_removed.second.empty());

    // *Only* second watch receives update.
    EXPECT_CALL(callbacks1, onConfigUpdate(_, _)).Times(0);
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version3");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version3");
  }
  // Second watch loses interest.
  {
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token2, {});
    EXPECT_TRUE(added_removed.first.empty());
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.second); // remove from subscription
  }
}

// Checks the following:
// First watch on a resource name ==> updateWatchInterest() returns "add it to subscription"
// Watch loses interest ==> "remove it from subscription"
// Second watch on that name ==> "add it to subscription"
TEST(WatchMapTest, AddRemoveAdd) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks1;
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks2;
  WatchMap watch_map;
  WatchMap::Token token1 = watch_map.addWatch(callbacks1);
  WatchMap::Token token2 = watch_map.addWatch(callbacks2);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
  envoy::api::v2::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  updated_resources.Add()->PackFrom(alice);

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // First watch receives update.
    expectDeltaAndSotwUpdate(callbacks1, {alice}, {}, "version1");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version1");
  }
  // First watch loses interest.
  {
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, {});
    EXPECT_TRUE(added_removed.first.empty());
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.second); // remove from subscription

    // (The xDS client should have responded to updateWatchInterest()'s return value by removing
    // Alice from the subscription, so onConfigUpdate() calls should be impossible right now.)
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token2, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // *Only* second watch receives update.
    EXPECT_CALL(callbacks1, onConfigUpdate(_, _)).Times(0);
    expectDeltaAndSotwUpdate(callbacks2, {alice}, {}, "version2");
    doDeltaAndSotwUpdate(watch_map, updated_resources, {}, "version2");
  }
}

// Tests that nothing breaks if an update arrives that we entirely do not care about.
TEST(WatchMapTest, UninterestingUpdate) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks;
  WatchMap watch_map;
  WatchMap::Token token = watch_map.addWatch(callbacks);
  watch_map.updateWatchInterest(token, {"alice"});

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> alice_update;
  envoy::api::v2::ClusterLoadAssignment alice;
  alice.set_cluster_name("alice");
  alice_update.Add()->PackFrom(alice);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> bob_update;
  envoy::api::v2::ClusterLoadAssignment bob;
  bob.set_cluster_name("bob");
  bob_update.Add()->PackFrom(bob);

  EXPECT_CALL(callbacks, onConfigUpdate(_, _)).Times(0);
  doDeltaAndSotwUpdate(watch_map, bob_update, {}, "version1");

  expectDeltaAndSotwUpdate(callbacks, {alice}, {}, "version2");
  doDeltaAndSotwUpdate(watch_map, alice_update, {}, "version2");

  EXPECT_CALL(callbacks, onConfigUpdate(_, _)).Times(0);
  doDeltaAndSotwUpdate(watch_map, bob_update, {}, "version3");

  // Clean removal of the watch: first update to "interested in nothing", then remove.
  watch_map.updateWatchInterest(token, {});
  EXPECT_TRUE(watch_map.removeWatch(token));
}

} // namespace
} // namespace Config
} // namespace Envoy
