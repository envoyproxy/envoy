#include <algorithm>
#include <string>

//#include "common/common/utility.h"
#include "common/stats/recent_lookups.h"

#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class RecentLookupsTest : public testing::Test {
protected:
  RecentLookupsTest() { //: recent_lookups_(time_system_) {
    const uint64_t years = 365 * 24 * 3600;
    time_system_.setSystemTime(SystemTime() + std::chrono::seconds(40 * years));
  }

  std::string joinLookups() {
    using ItemCount = std::pair<std::string, uint64_t>;
    std::vector<ItemCount> items;
    recent_lookups_.forEach([&items](absl::string_view item, uint64_t count) {
      items.push_back(ItemCount(std::string(item), count));
    });
    std::sort(items.begin(), items.end(), [](const ItemCount& a, const ItemCount& b) -> bool {
      if (a.second == b.second) {
        return a.first < b.first;
      }
      return a.second < b.second;
    });
    std::vector<std::string> accum;
    for (auto item : items) {
      accum.push_back(absl::StrCat(item.second, ": ", item.first));
    }
    return StringUtil::join(accum, " ");
  }

  Event::SimulatedTimeSystem time_system_;
  RecentLookups recent_lookups_;
};

TEST_F(RecentLookupsTest, Empty) { EXPECT_EQ("", joinLookups()); }

TEST_F(RecentLookupsTest, One) {
  recent_lookups_.lookup("Hello");
  EXPECT_EQ("1: Hello", joinLookups());
}

TEST_F(RecentLookupsTest, DropOne) {
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
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
}

TEST_F(RecentLookupsTest, RepeatDrop) {
  recent_lookups_.lookup("drop_early");
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
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
}

/*TEST_F(RecentLookupsTest, Log) {
  EXPECT_LOG_CONTAINS("warn", "Recent lookups for alpha", recent_lookups_.lookup("alpha"));
  EXPECT_NO_LOGS(recent_lookups_.lookup("beta"));
  time_system_.sleep(std::chrono::seconds(100));
  EXPECT_NO_LOGS(recent_lookups_.lookup("gamma"));
  time_system_.sleep(std::chrono::seconds(250));
  const Envoy::ExpectedLogMessages messages{{"warn", "gamma"}, {"warn", "delta"}};
  EXPECT_LOG_CONTAINS_ALL_OF(messages, recent_lookups_.lookup("delta"));
  }*/

} // namespace
} // namespace Stats
} // namespace Envoy
