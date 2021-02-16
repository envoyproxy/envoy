#include "common/common/cleanup.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(CleanupTest, ScopeExitCallback) {
  bool callback_fired = false;
  {
    Cleanup cleanup([&callback_fired] { callback_fired = true; });
    EXPECT_FALSE(callback_fired);
  }
  EXPECT_TRUE(callback_fired);
}

TEST(CleanupTest, Cancel) {
  bool callback_fired = false;
  {
    Cleanup cleanup([&callback_fired] { callback_fired = true; });
    EXPECT_FALSE(cleanup.canceled());
    cleanup.cancel();
    EXPECT_FALSE(callback_fired);
    EXPECT_TRUE(cleanup.canceled());
  }
  EXPECT_FALSE(callback_fired);
}

TEST(RaiiListElementTest, DeleteOnDestruction) {
  std::list<int> l;

  {
    EXPECT_EQ(l.size(), 0);
    RaiiListElement<int> rle(l, 1);
    EXPECT_EQ(l.size(), 1);
  }
  EXPECT_EQ(l.size(), 0);
}

TEST(RaiiListElementTest, CancelDelete) {
  std::list<int> l;

  {
    EXPECT_EQ(l.size(), 0);
    RaiiListElement<int> rle(l, 1);
    EXPECT_EQ(l.size(), 1);
    rle.cancel();
  }
  EXPECT_EQ(l.size(), 1);
}

TEST(RaiiListElementTest, DeleteOnErase) {
  std::list<int> l;

  {
    EXPECT_EQ(l.size(), 0);
    RaiiListElement<int> rle(l, 1);
    rle.erase();
    EXPECT_EQ(l.size(), 0);
  }
  EXPECT_EQ(l.size(), 0);
}

TEST(RaiiMapOfListElement, DeleteOnDestruction) {
  absl::flat_hash_map<int, std::list<int>> map;
  {
    EXPECT_EQ(map.size(), 0);
    RaiiMapOfListElement<int, int> element(map, 1, 1);
    EXPECT_EQ(map.size(), 1);
    auto it = map.find(1);
    ASSERT_NE(map.end(), it);
    EXPECT_EQ(it->second.size(), 1);
  }
  EXPECT_EQ(map.size(), 0);
}

TEST(RaiiMapOfListElementTest, CancelDelete) {
  absl::flat_hash_map<int, std::list<int>> map;

  {
    EXPECT_EQ(map.size(), 0);
    RaiiMapOfListElement<int, int> element(map, 1, 1);
    EXPECT_EQ(map.size(), 1);
    auto it = map.find(1);
    ASSERT_NE(map.end(), it);
    EXPECT_EQ(it->second.size(), 1);
    element.cancel();
  }
  EXPECT_EQ(map.size(), 1);
  auto it = map.find(1);
  ASSERT_NE(map.end(), it);
  EXPECT_EQ(it->second.size(), 1);
}

TEST(RaiiMapOfListElement, MultipleEntriesSameKey) {
  absl::flat_hash_map<int, std::list<int>> map;
  {
    EXPECT_EQ(map.size(), 0);
    RaiiMapOfListElement<int, int> element(map, 1, 1);
    EXPECT_EQ(map.size(), 1);
    auto it = map.find(1);
    ASSERT_NE(map.end(), it);
    EXPECT_EQ(it->second.size(), 1);
    {
      RaiiMapOfListElement<int, int> second_element(map, 1, 2);
      EXPECT_EQ(map.size(), 1);
      it = map.find(1);
      ASSERT_NE(map.end(), it);
      EXPECT_EQ(it->second.size(), 2);
    }
    it = map.find(1);
    ASSERT_NE(map.end(), it);
    EXPECT_EQ(it->second.size(), 1);
  }
  EXPECT_EQ(map.size(), 0);
}

} // namespace Envoy
