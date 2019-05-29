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

TEST(WatchMapTest, Basic) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks;
  WatchMap watch_map;
  WatchMap::Token token = watch_map.addWatch(callbacks);
  std::set<std::string> update_to({"alice", "bob"});
  std::pair<std::set<std::string>, std::set<std::string>> added_removed =
      watch_map.updateWatchInterest(token, update_to);
  EXPECT_EQ(update_to, added_removed.first);
  EXPECT_TRUE(added_removed.second.empty());

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

  watch_map.onConfigUpdate(updated_resources, "version1");

  std::pair<std::set<std::string>, std::set<std::string>> added_removed2 =
      watch_map.updateWatchInterest(token, {});

  EXPECT_TRUE(watch_map.removeWatch(token));
}

} // namespace
} // namespace Config
} // namespace Envoy
