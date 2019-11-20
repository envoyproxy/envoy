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
    EXPECT_FALSE(cleanup.cancelled());
    cleanup.cancel();
    EXPECT_FALSE(callback_fired);
    EXPECT_TRUE(cleanup.cancelled());
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

} // namespace Envoy
