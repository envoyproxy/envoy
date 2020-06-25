#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/cache_headers_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

// TODO(#9872): More tests for httpTime
// TODO: Replace global test cases with local variables / members

class HttpTimeTest : public testing::TestWithParam<const char*> {};

const char* const ok_times[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
    "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
};

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(ok_times));

TEST_P(HttpTimeTest, Ok) {
  Http::TestResponseHeaderMapImpl response_headers{{"date", GetParam()}};
  // Manually confirmed that 784111777 is 11/6/94, 8:46:37.
  EXPECT_EQ(784111777,
            SystemTime::clock::to_time_t(CacheHeadersUtils::httpTime(response_headers.Date())));
}

TEST(HttpTime, Null) { EXPECT_EQ(CacheHeadersUtils::httpTime(nullptr), SystemTime()); }

#define DURATION(s) std::chrono::seconds(s)
#define UNSET_DURATION absl::optional<SystemTime::duration>()
#define MAX_DURATION SystemTime::duration::max()

struct RequestCacheControlTestCase {
  absl::string_view cache_control_header;
  RequestCacheControl request_cache_control;
};

// RequestCacheControl = {must_validate, no_store, no_transform, only_if_cached, max_age, min_fresh,
// max_stale}
constexpr RequestCacheControlTestCase request_test_cases[] = {
    // Empty header
    {"", {false, false, false, false, UNSET_DURATION, UNSET_DURATION, UNSET_DURATION}},
    // Valid cache-control headers
    {"max-age=3600, min-fresh=10, no-transform, only-if-cached, no-store",
     {false, true, true, true, DURATION(3600), DURATION(10), UNSET_DURATION}},
    {"min-fresh=100, max-stale, no-cache",
     {true, false, false, false, UNSET_DURATION, DURATION(100), MAX_DURATION}},
    {"max-age=10, max-stale=50",
     {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}},
    // Quoted arguments are interpreted correctly
    {"max-age=\"3600\", min-fresh=\"10\", no-transform, only-if-cached, no-store",
     {false, true, true, true, DURATION(3600), DURATION(10), UNSET_DURATION}},
    {"max-age=\"10\", max-stale=\"50\", only-if-cached",
     {false, false, false, true, DURATION(10), UNSET_DURATION, DURATION(50)}},
    // Unknown directives are ignored
    {"max-age=10, max-stale=50, unknown-directive",
     {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}},
    {"max-age=10, max-stale=50, unknown-directive-with-arg=arg1",
     {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}},
    {"max-age=10, max-stale=50, unknown-directive-with-quoted-arg=\"arg1\"",
     {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}},
    {"max-age=10, max-stale=50, unknown-directive, unknown-directive-with-quoted-arg=\"arg1\"",
     {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}},
    // Invalid durations are ignored
    {"max-age=five, min-fresh=30, no-store",
     {false, true, false, false, UNSET_DURATION, DURATION(30), UNSET_DURATION}},
    {"max-age=five, min-fresh=30s, max-stale=-2",
     {false, false, false, false, UNSET_DURATION, UNSET_DURATION, UNSET_DURATION}},
    // Invalid parts of the header are ignored
    {"no-cache, ,,,fjfwioen3298, max-age=20, min-fresh=30=40",
     {true, false, false, false, DURATION(20), UNSET_DURATION, UNSET_DURATION}},
};

class RequestCacheControlTest : public testing::TestWithParam<RequestCacheControlTestCase> {};
INSTANTIATE_TEST_SUITE_P(RequestCacheControlTest, RequestCacheControlTest,
                         testing::ValuesIn(request_test_cases));

TEST_P(RequestCacheControlTest, RequestCacheControlTest) {
  absl::string_view cache_control_header = GetParam().cache_control_header;
  RequestCacheControl expected_request_cache_control = GetParam().request_cache_control;
  EXPECT_EQ(expected_request_cache_control,
            CacheHeadersUtils::requestCacheControl(cache_control_header));
}

struct ResponseCacheControlTestCase {
  absl::string_view cache_control_header;
  ResponseCacheControl response_cache_control;
};

// ResponseCacheControl = {must_validate, no_store, no_transform, no_stale, _public, max_age}
constexpr ResponseCacheControlTestCase response_test_cases[] = {
    // Empty header
    {"", {false, false, false, false, false, UNSET_DURATION}},
    // Valid cache-control headers
    {"s-maxage=1000, max-age=2000, proxy-revalidate, no-store",
     {false, true, false, true, false, DURATION(1000)}},
    {"max-age=500, must-revalidate, no-cache, no-transform",
     {true, false, true, true, false, DURATION(500)}},
    {"s-maxage=10, private=content-length, no-cache=content-encoding",
     {true, true, false, false, false, DURATION(10)}},
    {"private", {false, true, false, false, false, UNSET_DURATION}},
    {"public, max-age=0", {false, false, false, false, true, DURATION(0)}},
    // Quoted arguments are interpreted correctly
    {"s-maxage=\"20\", max-age=\"10\", public", {false, false, false, false, true, DURATION(20)}},
    {"max-age=\"50\", private", {false, true, false, false, false, DURATION(50)}},
    {"s-maxage=\"0\"", {false, false, false, false, false, DURATION(0)}},
    // Unknown directives are ignored
    {"private, no-cache, max-age=30, unknown-directive",
     {true, true, false, false, false, DURATION(30)}},
    {"private, no-cache, max-age=30, unknown-directive-with-arg=arg",
     {true, true, false, false, false, DURATION(30)}},
    {"private, no-cache, max-age=30, unknown-directive-with-quoted-arg=\"arg\"",
     {true, true, false, false, false, DURATION(30)}},
    {"private, no-cache, max-age=30, unknown-directive, unknown-directive-with-quoted-arg=\"arg\"",
     {true, true, false, false, false, DURATION(30)}},
    // Invalid durations are ignored
    {"max-age=five", {false, false, false, false, false, UNSET_DURATION}},
    {"max-age=10s, private", {false, true, false, false, false, UNSET_DURATION}},
    {"s-maxage=\"50s\", max-age=\"zero\", no-cache",
     {true, false, false, false, false, UNSET_DURATION}},
    {"s-maxage=five, max-age=10, no-transform", {false, false, true, false, false, DURATION(10)}},
    // Invalid parts of the header are ignored
    {"no-cache, ,,,fjfwioen3298, max-age=20", {true, false, false, false, false, DURATION(20)}}};

class ResponseCacheControlTest : public testing::TestWithParam<ResponseCacheControlTestCase> {};

INSTANTIATE_TEST_SUITE_P(ResponseCacheControlTest, ResponseCacheControlTest,
                         testing::ValuesIn(response_test_cases));

TEST_P(ResponseCacheControlTest, ResponseCacheControlTest) {
  absl::string_view cache_control_header = GetParam().cache_control_header;
  ResponseCacheControl expected_response_cache_control = GetParam().response_cache_control;
  EXPECT_EQ(expected_response_cache_control,
            CacheHeadersUtils::responseCacheControl(cache_control_header));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
