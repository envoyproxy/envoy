#include <algorithm>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/stats/recent_lookups.h"

#include "test/test_common/logging.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class RecentLookupsTest : public testing::Test {
protected:
  std::string joinLookups() {
    using ItemCount = std::pair<std::string, uint64_t>;
    std::vector<ItemCount> items;
    recent_lookups_.forEach([&items](absl::string_view item, uint64_t count) {
      items.emplace_back(ItemCount(std::string(item), count));
    });
    std::sort(items.begin(), items.end(), [](const ItemCount& a, const ItemCount& b) -> bool {
      if (a.second == b.second) {
        return a.first < b.first;
      }
      return a.second < b.second;
    });
    std::vector<std::string> accum;
    accum.reserve(items.size());
    for (const auto& item : items) {
      accum.push_back(absl::StrCat(item.second, ": ", item.first));
    }
    return absl::StrJoin(accum, " ");
  }

  RecentLookups recent_lookups_;
};

TEST_F(RecentLookupsTest, Empty) { EXPECT_EQ("", joinLookups()); }

TEST_F(RecentLookupsTest, One) {
  recent_lookups_.lookup("Hello");
  EXPECT_EQ("", joinLookups());
  recent_lookups_.setCapacity(10);
  EXPECT_EQ(1, recent_lookups_.total());
  recent_lookups_.lookup("Hello");
  EXPECT_EQ(2, recent_lookups_.total());
  EXPECT_EQ("1: Hello", joinLookups());

  recent_lookups_.clear();
  EXPECT_EQ("", joinLookups());
  EXPECT_EQ(0, recent_lookups_.total());
  recent_lookups_.lookup("Hello");
  EXPECT_EQ(1, recent_lookups_.total());
  EXPECT_EQ("1: Hello", joinLookups());
  recent_lookups_.setCapacity(0);
  EXPECT_EQ("", joinLookups());
  EXPECT_EQ(1, recent_lookups_.total());
}

TEST_F(RecentLookupsTest, DropOne) {
  recent_lookups_.setCapacity(10);
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
  }
  EXPECT_EQ("1: lookup1 "
            "1: lookup10 "
            "1: lookup2 "
            "1: lookup3 "
            "1: lookup4 "
            "1: lookup5 "
            "1: lookup6 "
            "1: lookup7 "
            "1: lookup8 "
            "1: lookup9",
            joinLookups());
  recent_lookups_.clear();
  EXPECT_EQ("", joinLookups());
}

TEST_F(RecentLookupsTest, RepeatDrop) {
  recent_lookups_.setCapacity(10);
  recent_lookups_.lookup("drop_early");
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    recent_lookups_.lookup(absl::StrCat("lookup", i));
  }
  recent_lookups_.lookup("add_late");
  EXPECT_EQ("1: add_late "
            "2: lookup10 "
            "2: lookup2 "
            "2: lookup3 "
            "2: lookup4 "
            "2: lookup5 "
            "2: lookup6 "
            "2: lookup7 "
            "2: lookup8 "
            "2: lookup9",
            joinLookups());
  recent_lookups_.clear();
  EXPECT_EQ("", joinLookups());
}

} // namespace
} // namespace Stats
} // namespace Envoy
