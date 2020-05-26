#include "server/admin/config_tracker_impl.h"

#include "test/mocks/common.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class ConfigTrackerImplTest : public testing::Test {
public:
  ConfigTrackerImplTest() : cbs_map(tracker.getCallbacksMap()) {
    EXPECT_TRUE(cbs_map.empty());
    test_cb = [this] {
      called = true;
      return test_msg();
    };
  }

  ProtobufTypes::MessagePtr test_msg() { return std::make_unique<ProtobufWkt::Any>(); }

  ~ConfigTrackerImplTest() override = default;

  ConfigTrackerImpl tracker;
  const std::map<std::string, ConfigTracker::Cb>& cbs_map;
  ConfigTracker::Cb test_cb;
  bool called{false};
  std::string test_string{"foo"};
};

TEST_F(ConfigTrackerImplTest, Basic) {
  EXPECT_EQ(0, cbs_map.size());
  auto entry_owner = tracker.add("test_key", test_cb);
  EXPECT_EQ(1, cbs_map.size());
  EXPECT_NE(nullptr, entry_owner);
  EXPECT_NE(nullptr, cbs_map.begin()->second());
  EXPECT_TRUE(called);
}

TEST_F(ConfigTrackerImplTest, DestroyHandleBeforeTracker) {
  auto entry_owner = tracker.add("test_key", test_cb);
  EXPECT_EQ(1, cbs_map.size());
  entry_owner.reset();
  EXPECT_EQ(0, cbs_map.size());
}

TEST_F(ConfigTrackerImplTest, DestroyTrackerBeforeHandle) {
  std::shared_ptr<ConfigTracker> tracker_ptr = std::make_shared<ConfigTrackerImpl>();
  auto entry_owner = tracker.add("test_key", test_cb);
  tracker_ptr.reset();
  entry_owner.reset(); // Shouldn't explode
}

TEST_F(ConfigTrackerImplTest, AddDuplicate) {
  auto entry_owner = tracker.add("test_key", test_cb);
  EXPECT_EQ(nullptr, tracker.add("test_key", test_cb));
  entry_owner.reset();
  // Now we can add it
  EXPECT_NE(nullptr, tracker.add("test_key", test_cb));
}

TEST_F(ConfigTrackerImplTest, OperationsWithinCallback) {
  ConfigTracker::EntryOwnerPtr owner1, owner2;
  owner1 = tracker.add("test_key", [&] {
    owner2 = tracker.add("test_key2", [&] {
      owner1.reset();
      return test_msg();
    });
    return test_msg();
  });
  EXPECT_EQ(1, cbs_map.size());
  EXPECT_NE(nullptr, owner1);
  EXPECT_NE(nullptr, cbs_map.at("test_key")());
  EXPECT_EQ(2, cbs_map.size());
  EXPECT_NE(nullptr, owner2);
  EXPECT_NE(nullptr, cbs_map.at("test_key2")());
  EXPECT_EQ(1, cbs_map.size());
  EXPECT_EQ(0, cbs_map.count("test_key"));
}

} // namespace Server
} // namespace Envoy
