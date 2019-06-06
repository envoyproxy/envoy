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

using absl::InfinitePast;
using absl::UTCTimeZone;
using Http::TestHeaderMapImpl;
using std::string;
using testing::TestWithParam;
using testing::ValuesIn;

class HttpTimeTest : public TestWithParam<string> {
protected:
  TestHeaderMapImpl response_headers_{{"date", GetParam()}};
};

const string ok_times[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
    "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
};

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, ValuesIn(ok_times));

TEST_P(HttpTimeTest, Ok) {
  EXPECT_EQ(FormatTime(httpTime(response_headers_.Date()), UTCTimeZone()),
            "1994-11-06T08:49:37+00:00");
}

TEST(HttpTime, Null) { EXPECT_EQ(httpTime(nullptr), InfinitePast()); }
} // namespace
} // namespace Internal
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
