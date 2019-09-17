#include <algorithm>
#include <string>

#include "common/common/utility.h"
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
  RecentLookupsTest() : recent_lookups_(time_system_) {
    const uint64_t years = 365 * 24 * 3600;
    time_system_.setSystemTime(SystemTime() + std::chrono::seconds(40 * years));
  }

  std::string joinLookups() {
    std::vector<std::string> accum;
    recent_lookups_.forEach([&accum](absl::string_view item, SystemTime time) {
      DateFormatter formatter("%Y-%m-%d,%H:%M:%S");
      accum.emplace_back(absl::StrCat(formatter.fromTime(time), ";Item=", item));
    });
    std::sort(accum.begin(), accum.end());
    return StringUtil::join(accum, " ");
  }

  Event::SimulatedTimeSystem time_system_;
  RecentLookups recent_lookups_;
};

TEST_F(RecentLookupsTest, Empty) { EXPECT_EQ("", joinLookups()); }

TEST_F(RecentLookupsTest, One) {
  recent_lookups_.lookup("Hello");
  EXPECT_EQ("2009-12-22,00:00:00;Item=Hello", joinLookups());
}

TEST_F(RecentLookupsTest, DropOne) {
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
  }
  EXPECT_EQ("2009-12-22,00:00:01;Item=lookup1 "
            "2009-12-22,00:00:02;Item=lookup2 "
            "2009-12-22,00:00:03;Item=lookup3 "
            "2009-12-22,00:00:04;Item=lookup4 "
            "2009-12-22,00:00:05;Item=lookup5 "
            "2009-12-22,00:00:06;Item=lookup6 "
            "2009-12-22,00:00:07;Item=lookup7 "
            "2009-12-22,00:00:08;Item=lookup8 "
            "2009-12-22,00:00:09;Item=lookup9 "
            "2009-12-22,00:00:10;Item=lookup10",
            joinLookups());
}

TEST_F(RecentLookupsTest, RepeatDrop) {
  for (int i = 0; i < 11; ++i) {
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
    recent_lookups_.lookup(absl::StrCat("lookup", i));
    time_system_.sleep(std::chrono::seconds(1));
  }
  EXPECT_EQ("2009-12-22,00:00:12;Item=lookup6 "
            "2009-12-22,00:00:13;Item=lookup6 "
            "2009-12-22,00:00:14;Item=lookup7 "
            "2009-12-22,00:00:15;Item=lookup7 "
            "2009-12-22,00:00:16;Item=lookup8 "
            "2009-12-22,00:00:17;Item=lookup8 "
            "2009-12-22,00:00:18;Item=lookup9 "
            "2009-12-22,00:00:19;Item=lookup9 "
            "2009-12-22,00:00:20;Item=lookup10 "
            "2009-12-22,00:00:21;Item=lookup10",
            joinLookups());
}

TEST_F(RecentLookupsTest, Log) {
  EXPECT_LOG_CONTAINS("warn", "Recent lookups for alpha", recent_lookups_.lookup("alpha"));
  EXPECT_NO_LOGS(recent_lookups_.lookup("beta"));
  time_system_.sleep(std::chrono::seconds(100));
  EXPECT_NO_LOGS(recent_lookups_.lookup("gamma"));
  time_system_.sleep(std::chrono::seconds(250));
  const Envoy::ExpectedLogMessages messages{{"warn", "gamma"}, {"warn", "delta"}};
  EXPECT_LOG_CONTAINS_ALL_OF(messages, recent_lookups_.lookup("delta"));
}

} // namespace
} // namespace Stats
} // namespace Envoy
