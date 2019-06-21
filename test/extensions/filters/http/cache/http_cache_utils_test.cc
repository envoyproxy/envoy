#include <vector>

#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace Internal {
namespace {

class HttpTimeTest : public testing::TestWithParam<std::string> {
protected:
  Http::TestHeaderMapImpl response_headers_{{"date", GetParam()}};
};

const std::vector<std::string> ok_times = {
    "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
    "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
};

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(ok_times));

TEST_P(HttpTimeTest, Ok) {
  const std::time_t time = SystemTime::clock::to_time_t(httpTime(response_headers_.Date()));
  EXPECT_STREQ(ctime(&time), "Sun Nov  6 08:49:37 1994\n");
}

TEST(HttpTime, Null) { EXPECT_EQ(httpTime(nullptr), SystemTime::min()); }

TEST(HttpTime, Ok) { EXPECT_EQ(httpTime(nullptr), SystemTime::min()); }

TEST(EffectiveMaxAge, Ok) {
  EXPECT_EQ(std::chrono::seconds(3600), effectiveMaxAge("public, max-age=3600"));
}

TEST(EffectiveMaxAge, NegativeMaxAge) {
  EXPECT_EQ(SystemTime::duration::zero(), effectiveMaxAge("public, max-age=-1"));
}

} // namespace
} // namespace Internal
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
