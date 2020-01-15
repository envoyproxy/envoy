#include <vector>

#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

// TODO(toddmgreer): Add tests for eat* functions
// TODO(toddmgreer): More tests for httpTime, effectiveMaxAge

class HttpTimeTest : public testing::TestWithParam<const char*> {
protected:
  Http::TestHeaderMapImpl response_headers_{{"date", GetParam()}};
};

const char* const ok_times[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
    "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
};

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(ok_times));

TEST_P(HttpTimeTest, Ok) {
  const std::time_t time = SystemTime::clock::to_time_t(Utils::httpTime(response_headers_.Date()));
  EXPECT_STREQ(ctime(&time), "Sun Nov  6 08:49:37 1994\n");
}

TEST(HttpTime, Null) { EXPECT_EQ(Utils::httpTime(nullptr), SystemTime()); }

struct EffectiveMaxAgeParams {
  absl::string_view cache_control;
  int effective_max_age_secs;
};

EffectiveMaxAgeParams params[] = {
    {"public, max-age=3600", 3600},
    {"public, max-age=-1", 0},
    {"max-age=20", 20},
    {"max-age=86400, public", 86400},
    {"public,max-age=\"0\"", 0},
    {"public,max-age=8", 8},
    {"public,max-age=3,no-cache", 0},
    {"s-maxage=0", 0},
    {"max-age=10,s-maxage=0", 0},
    {"s-maxage=10", 10},
    {"no-cache", 0},
    {"max-age=0", 0},
    {"no-cache", 0},
    {"public", 0},
    // TODO(toddmgreer): parse quoted forms
    // {"max-age=20, s-maxage=\"25\"",25},
    // {"public,max-age=\"8\",foo=11",8},
    // {"public,max-age=\"8\",bar=\"11\"",8},
    // TODO(toddmgreer): parse public/private
    // {"private,max-age=10",0}
    // {"private",0},
    // {"private,s-maxage=8",0},
};

class EffectiveMaxAgeTest : public testing::TestWithParam<EffectiveMaxAgeParams> {};

INSTANTIATE_TEST_SUITE_P(EffectiveMaxAgeTest, EffectiveMaxAgeTest, testing::ValuesIn(params));

TEST_P(EffectiveMaxAgeTest, EffectiveMaxAgeTest) {
  EXPECT_EQ(Utils::effectiveMaxAge(GetParam().cache_control),
            std::chrono::seconds(GetParam().effective_max_age_secs));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
