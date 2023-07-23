#include "source/common/common/matchers.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/mocks/common.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class ConfigTrackerImplTest : public testing::Test {
public:
  ConfigTrackerImplTest() : cbs_map(tracker.getCallbacksMap()) {
    EXPECT_TRUE(cbs_map.empty());
    test_cb = [this](const Matchers::StringMatcher&) {
      called = true;
      return testMsg();
    };
  }

  ProtobufTypes::MessagePtr testMsg() { return std::make_unique<ProtobufWkt::Any>(); }

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
  EXPECT_NE(nullptr, cbs_map.begin()->second(Matchers::UniversalStringMatcher()));
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
  owner1 = tracker.add("test_key", [&](const Matchers::StringMatcher&) {
    owner2 = tracker.add("test_key2", [&](const Matchers::StringMatcher&) {
      owner1.reset();
      return testMsg();
    });
    return testMsg();
  });
  EXPECT_EQ(1, cbs_map.size());
  EXPECT_NE(nullptr, owner1);
  EXPECT_NE(nullptr, cbs_map.at("test_key")(Matchers::UniversalStringMatcher()));
  EXPECT_EQ(2, cbs_map.size());
  EXPECT_NE(nullptr, owner2);
  EXPECT_NE(nullptr, cbs_map.at("test_key2")(Matchers::UniversalStringMatcher()));
  EXPECT_EQ(1, cbs_map.size());
  EXPECT_EQ(0, cbs_map.count("test_key"));
}

} // namespace Server
} // namespace Envoy
