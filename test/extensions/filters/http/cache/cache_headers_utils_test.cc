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

#define DURATION(s) std::chrono::seconds(s)
#define UNSET_DURATION absl::optional<SystemTime::duration>()
#define MAX_DURATION SystemTime::duration::max()

struct TestRequestCacheControl : public RequestCacheControl {
  TestRequestCacheControl(bool must_validate, bool no_store, bool no_transform, bool only_if_cached,
                          OptionalDuration max_age, OptionalDuration min_fresh,
                          OptionalDuration max_stale) {
    must_validate_ = must_validate;
    no_store_ = no_store;
    no_transform_ = no_transform;
    only_if_cached_ = only_if_cached;
    max_age_ = max_age;
    min_fresh_ = min_fresh;
    max_stale_ = max_stale;
  }
};

struct TestResponseCacheControl : public ResponseCacheControl {
  TestResponseCacheControl(bool must_validate, bool no_store, bool no_transform, bool no_stale,
                           bool is_public, OptionalDuration max_age) {
    must_validate_ = must_validate;
    no_store_ = no_store;
    no_transform_ = no_transform;
    no_stale_ = no_stale;
    is_public_ = is_public;
    max_age_ = max_age;
  }
};

struct RequestCacheControlTestCase {
  absl::string_view cache_control_header;
  TestRequestCacheControl request_cache_control;
};

struct ResponseCacheControlTestCase {
  absl::string_view cache_control_header;
  TestResponseCacheControl response_cache_control;
};

class RequestCacheControlTest : public testing::TestWithParam<RequestCacheControlTestCase> {
public:
  static auto getTestCases() {
    // clang-format off
    // RequestCacheControl = {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
    static const RequestCacheControlTestCase test_cases[] = {
        // Empty header
        {
          "", 
          {false, false, false, false, UNSET_DURATION, UNSET_DURATION, UNSET_DURATION}
        },
        // Valid cache-control headers
        {
          "max-age=3600, min-fresh=10, no-transform, only-if-cached, no-store",
          {false, true, true, true, DURATION(3600), DURATION(10), UNSET_DURATION}
        },
        {
          "min-fresh=100, max-stale, no-cache",
          {true, false, false, false, UNSET_DURATION, DURATION(100), MAX_DURATION}
        },
        {
          "max-age=10, max-stale=50",
          {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        // Quoted arguments are interpreted correctly
        {
          "max-age=\"3600\", min-fresh=\"10\", no-transform, only-if-cached, no-store",
          {false, true, true, true, DURATION(3600), DURATION(10), UNSET_DURATION}
        },
        {
          "max-age=\"10\", max-stale=\"50\", only-if-cached",
          {false, false, false, true, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        // Unknown directives are ignored
        {
          "max-age=10, max-stale=50, unknown-directive",
          {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-arg=arg1",
          {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-quoted-arg=\"arg1\"",
          {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive, unknown-directive-with-quoted-arg=\"arg1\"",
          {false, false, false, false, DURATION(10), UNSET_DURATION, DURATION(50)}
        },
        // Invalid durations are ignored
        {
          "max-age=five, min-fresh=30, no-store",
          {false, true, false, false, UNSET_DURATION, DURATION(30), UNSET_DURATION}
        },
        {
          "max-age=five, min-fresh=30s, max-stale=-2",
          {false, false, false, false, UNSET_DURATION, UNSET_DURATION, UNSET_DURATION}
        },
        {
          "max-age=\"", 
          {false, false, false, false, UNSET_DURATION, UNSET_DURATION, UNSET_DURATION}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20, min-fresh=30=40",
          {true, false, false, false, DURATION(20), UNSET_DURATION, UNSET_DURATION}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store",
          {true, true, false, false, DURATION(10), UNSET_DURATION, UNSET_DURATION}
        },
    };
    // clang-format on

    return &test_cases;
  }
};

class ResponseCacheControlTest : public testing::TestWithParam<ResponseCacheControlTestCase> {
public:
  static auto getTestCases() {
    // clang-format off
    // ResponseCacheControl = {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
    static const ResponseCacheControlTestCase test_cases[] = {
        // Empty header
        {
          "", 
          {false, false, false, false, false, UNSET_DURATION}
        },
        // Valid cache-control headers
        {
          "s-maxage=1000, max-age=2000, proxy-revalidate, no-store",
          {false, true, false, true, false, DURATION(1000)}
        },
        {
          "max-age=500, must-revalidate, no-cache, no-transform",
          {true, false, true, true, false, DURATION(500)}
        },
        {
          "s-maxage=10, private=content-length, no-cache=content-encoding",
          {true, true, false, false, false, DURATION(10)}
        },
        {
          "private", 
          {false, true, false, false, false, UNSET_DURATION}
        },
        {
          "public, max-age=0", 
          {false, false, false, false, true, DURATION(0)}
        },
        // Quoted arguments are interpreted correctly
        {
          "s-maxage=\"20\", max-age=\"10\", public", 
          {false, false, false, false, true, DURATION(20)}
        },
        {
          "max-age=\"50\", private", 
          {false, true, false, false, false, DURATION(50)}
        },
        {
          "s-maxage=\"0\"", 
          {false, false, false, false, false, DURATION(0)}
        },
        // Unknown directives are ignored
        {
          "private, no-cache, max-age=30, unknown-directive",
          {true, true, false, false, false, DURATION(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-arg=arg",
          {true, true, false, false, false, DURATION(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-quoted-arg=\"arg\"",
          {true, true, false, false, false, DURATION(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive, unknown-directive-with-quoted-arg=\"arg\"",
          {true, true, false, false, false, DURATION(30)}
        },
        // Invalid durations are ignored
        {
          "max-age=five", 
          {false, false, false, false, false, UNSET_DURATION}
        },
        {
          "max-age=10s, private", 
          {false, true, false, false, false, UNSET_DURATION}
        },
        {
          "s-maxage=\"50s\", max-age=\"zero\", no-cache",
          {true, false, false, false, false, UNSET_DURATION}
        },
        {
          "s-maxage=five, max-age=10, no-transform", 
          {false, false, true, false, false, DURATION(10)}
        },
        {
          "max-age=\"", 
          {false, false, false, false, false, UNSET_DURATION}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20", 
          {true, false, false, false, false, DURATION(20)}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store", 
          {true, true, false, false, false, DURATION(10)}
        },
    };
    // clang-format on

    return &test_cases;
  }
};

// TODO(#9872): More tests for httpTime
class HttpTimeTest : public testing::TestWithParam<std::string> {
public:
  static auto getOkTestCases() {
    const static std::string ok_times[] = {
        "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
        "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
        "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
    };
    return &ok_times;
  }
};

INSTANTIATE_TEST_SUITE_P(RequestCacheControlTest, RequestCacheControlTest,
                         testing::ValuesIn(*RequestCacheControlTest::getTestCases()));

TEST_P(RequestCacheControlTest, RequestCacheControlTest) {
  absl::string_view cache_control_header = GetParam().cache_control_header;
  RequestCacheControl expected_request_cache_control = GetParam().request_cache_control;
  EXPECT_EQ(expected_request_cache_control, RequestCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(ResponseCacheControlTest, ResponseCacheControlTest,
                         testing::ValuesIn(*ResponseCacheControlTest::getTestCases()));

TEST_P(ResponseCacheControlTest, ResponseCacheControlTest) {
  absl::string_view cache_control_header = GetParam().cache_control_header;
  ResponseCacheControl expected_response_cache_control = GetParam().response_cache_control;
  EXPECT_EQ(expected_response_cache_control, ResponseCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(*HttpTimeTest::getOkTestCases()));

TEST_P(HttpTimeTest, Ok) {
  Http::TestResponseHeaderMapImpl response_headers{{"date", GetParam()}};
  // Manually confirmed that 784111777 is 11/6/94, 8:46:37.
  EXPECT_EQ(784111777,
            SystemTime::clock::to_time_t(CacheHeadersUtils::httpTime(response_headers.Date())));
}

TEST(HttpTime, Null) { EXPECT_EQ(CacheHeadersUtils::httpTime(nullptr), SystemTime()); }

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
