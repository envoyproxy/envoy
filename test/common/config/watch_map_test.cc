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

// Tests the simple case of a single watch. Checks that the watch will not be told of updates to
// resources it doesn't care about. Checks that the watch can later decide it does care about them,
// and then receive subsequent updates to them.
TEST(WatchMapTest, Basic) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks;
  WatchMap watch_map;
  WatchMap::Token token = watch_map.addWatch(callbacks);

  {
    // The watch is interested in Alice and Bob.
    std::set<std::string> update_to({"alice", "bob"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token, update_to);
    EXPECT_EQ(update_to, added_removed.first);
    EXPECT_TRUE(added_removed.second.empty());

    // The update is going to contain Bob and Carol.
    envoy::api::v2::ClusterLoadAssignment expected_assignment;
    expected_assignment.set_cluster_name("bob");
    envoy::api::v2::ClusterLoadAssignment unexpected_assignment;
    unexpected_assignment.set_cluster_name("carol");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(expected_assignment);
    updated_resources.Add()->PackFrom(unexpected_assignment);

    EXPECT_CALL(callbacks, onConfigUpdate(_, "version1"))
        .WillOnce(Invoke(
            [expected_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                  const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, expected_assignment));
            }));

    watch_map.onConfigUpdate(updated_resources, "version1"); // TODO add delta
  }

  {
    // The watch is interested in Bob, Carol, Dave, Eve.
    std::set<std::string> update_to({"bob", "carol", "dave", "eve"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token, update_to);
    EXPECT_EQ(std::set<std::string>({"carol", "dave", "eve"}), added_removed.first);
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.second);

    // The update is going to contain Alice, Carol, Dave.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");
    envoy::api::v2::ClusterLoadAssignment carol_assignment;
    carol_assignment.set_cluster_name("carol");
    envoy::api::v2::ClusterLoadAssignment dave_assignment;
    dave_assignment.set_cluster_name("dave");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);
    updated_resources.Add()->PackFrom(carol_assignment);
    updated_resources.Add()->PackFrom(dave_assignment);

    EXPECT_CALL(callbacks, onConfigUpdate(_, "version2"))
        .WillOnce(Invoke(
            [carol_assignment, dave_assignment](
                const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
              EXPECT_EQ(2, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, carol_assignment));
              resources[1].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, dave_assignment));
            }));

    watch_map.onConfigUpdate(updated_resources, "version2"); // TODO add delta
  }

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

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // First watch receives update.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);

    EXPECT_CALL(callbacks1, onConfigUpdate(_, "version1"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    watch_map.onConfigUpdate(updated_resources, "version1"); // TODO add delta
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token2, update_to);
    EXPECT_TRUE(added_removed.first.empty()); // nothing happens
    EXPECT_TRUE(added_removed.second.empty());

    // Both watches receive update.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);

    EXPECT_CALL(callbacks1, onConfigUpdate(_, "version2"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    EXPECT_CALL(callbacks2, onConfigUpdate(_, "version2"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    watch_map.onConfigUpdate(updated_resources, "version2"); // TODO add delta
  }
  // First watch loses interest.
  {
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, {});
    EXPECT_TRUE(added_removed.first.empty()); // nothing happens
    EXPECT_TRUE(added_removed.second.empty());

    // *Only* second watch receives update.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);

    EXPECT_CALL(callbacks1, onConfigUpdate(_, "version3")).Times(0);
    EXPECT_CALL(callbacks2, onConfigUpdate(_, "version3"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    watch_map.onConfigUpdate(updated_resources, "version3"); // TODO add delta
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

  // First watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // First watch receives update.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);

    EXPECT_CALL(callbacks1, onConfigUpdate(_, "version1"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    watch_map.onConfigUpdate(updated_resources, "version1"); // TODO add delta
  }
  // First watch loses interest.
  {
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token1, {});
    EXPECT_TRUE(added_removed.first.empty());
    EXPECT_EQ(std::set<std::string>({"alice"}), added_removed.second); // remove from subscription

    // (The xDS client should have responded to updateWatchInterest()'s return value by removing
    // alice from the subscription, so onConfigUpdate() calls should be impossible right now.)
  }
  // Second watch becomes interested.
  {
    std::set<std::string> update_to({"alice"});
    std::pair<std::set<std::string>, std::set<std::string>> added_removed =
        watch_map.updateWatchInterest(token2, update_to);
    EXPECT_EQ(update_to, added_removed.first); // add to subscription
    EXPECT_TRUE(added_removed.second.empty());

    // *Only* second watch receives update.
    envoy::api::v2::ClusterLoadAssignment alice_assignment;
    alice_assignment.set_cluster_name("alice");

    Protobuf::RepeatedPtrField<ProtobufWkt::Any> updated_resources;
    updated_resources.Add()->PackFrom(alice_assignment);

    EXPECT_CALL(callbacks1, onConfigUpdate(_, "version2")).Times(0);
    EXPECT_CALL(callbacks2, onConfigUpdate(_, "version2"))
        .WillOnce(
            Invoke([alice_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment gotten_assignment;
              resources[0].UnpackTo(&gotten_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(gotten_assignment, alice_assignment));
            }));
    watch_map.onConfigUpdate(updated_resources, "version2"); // TODO add delta
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
