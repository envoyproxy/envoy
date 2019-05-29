#include <memory>

#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "common/config/watch_map.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace Envoy {
namespace Config {
namespace {

TEST(WatchMapTest, Basic) {
  MockSubscriptionCallbacks callbacks;
  WatchMap watch_map;
  WatchToken token = watch_map.addWatch(callbacks);
  std::set<std::string> update_to({"alice", "bob"});
  std::pair<std::set<std::string>, std::set<std::string>> added_removed =
      updateWatchInterest(token, update_to);
  EXPECT_EQ(update_to, added_removed.first);
  EXPECT_TRUE(added_removed.second.empty());

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> expected_resources;

  // TODO TODO can it be this?  EXPECT_CALL(callbacks,
  // onConfigUpdate(RepeatedProtoEq(expected_resources), "version1"));

  EXPECT_CALL(callbacks, onConfigUpdate(_, "version1"))
      .WillOnce(Invoke(
          [](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
            EXPECT_EQ(1, resources.size());
            envoy::api::v2::ClusterLoadAssignment expected_assignment;
            resources[0].UnpackTo(&expected_assignment);
            EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
          }));

  watch_map.onConfigUpdate(expected_resources, "version1");

  std::pair<std::set<std::string>, std::set<std::string>> added_removed2 =
      updateWatchInterest(token, {});

  EXPECT_TRUE(watch_map.removeWatch(token));
}

} // namespace
} // namespace Config
} // namespace Envoy
