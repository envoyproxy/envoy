#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/common/macros.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/cache_headers_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

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
  static const std::vector<RequestCacheControlTestCase>& getTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<RequestCacheControlTestCase>,
        // Empty header
        {
          "",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        // Valid cache-control headers
        {
          "max-age=3600, min-fresh=10, no-transform, only-if-cached, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, true, true, std::chrono::seconds(3600), std::chrono::seconds(10), absl::nullopt}
        },
        {
          "min-fresh=100, max-stale, no-cache",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, false, false, false, absl::nullopt, std::chrono::seconds(100), SystemTime::duration::max()}
        },
        {
          "max-age=10, max-stale=50",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Quoted arguments are interpreted correctly
        {
          "max-age=\"3600\", min-fresh=\"10\", no-transform, only-if-cached, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, true, true, std::chrono::seconds(3600), std::chrono::seconds(10), absl::nullopt}
        },
        {
          "max-age=\"10\", max-stale=\"50\", only-if-cached",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, true, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Unknown directives are ignored
        {
          "max-age=10, max-stale=50, unknown-directive",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-arg=arg1",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-quoted-arg=\"arg1\"",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive, unknown-directive-with-quoted-arg=\"arg1\"",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Invalid durations are ignored
        {
          "max-age=five, min-fresh=30, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, false, false, absl::nullopt, std::chrono::seconds(30), absl::nullopt}
        },
        {
          "max-age=five, min-fresh=30s, max-stale=-2",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        {
          "max-age=\"", 
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20, min-fresh=30=40",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, false, false, false, std::chrono::seconds(20), absl::nullopt, absl::nullopt}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, true, false, false, std::chrono::seconds(10), absl::nullopt, absl::nullopt}
        },
    );
    // clang-format on
  }
};

class ResponseCacheControlTest : public testing::TestWithParam<ResponseCacheControlTestCase> {
public:
  static const std::vector<ResponseCacheControlTestCase>& getTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<ResponseCacheControlTestCase>,
        // Empty header
        {
          "", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        // Valid cache-control headers
        {
          "s-maxage=1000, max-age=2000, proxy-revalidate, no-store",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, true, false, std::chrono::seconds(1000)}
        },
        {
          "max-age=500, must-revalidate, no-cache, no-transform",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, true, true, false, std::chrono::seconds(500)}
        },
        {
          "s-maxage=10, private=content-length, no-cache=content-encoding",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(10)}
        },
        {
          "private",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, absl::nullopt}
        },
        {
          "public, max-age=0",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, true, std::chrono::seconds(0)}
        },
        // Quoted arguments are interpreted correctly
        {
          "s-maxage=\"20\", max-age=\"10\", public",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, true, std::chrono::seconds(20)}
        },
        {
          "max-age=\"50\", private",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, std::chrono::seconds(50)}
        },
        {
          "s-maxage=\"0\"", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, std::chrono::seconds(0)}
        },
        // Unknown directives are ignored
        {
          "private, no-cache, max-age=30, unknown-directive",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-arg=arg",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-quoted-arg=\"arg\"",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive, unknown-directive-with-quoted-arg=\"arg\"",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        // Invalid durations are ignored
        {
          "max-age=five", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        {
          "max-age=10s, private", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, absl::nullopt}
        },
        {
          "s-maxage=\"50s\", max-age=\"zero\", no-cache",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, false, false, false, absl::nullopt}
        },
        {
          "s-maxage=five, max-age=10, no-transform", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, true, false, false, std::chrono::seconds(10)}
        },
        {
          "max-age=\"", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, false, false, false, std::chrono::seconds(20)}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(10)}
        },
    );
    // clang-format on
  }
};

// TODO(#9872): More tests for httpTime
class HttpTimeTest : public testing::TestWithParam<std::string> {
public:
  static const std::vector<std::string>& getOkTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>,
        "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
        "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
        "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
    );
    // clang-format on
  }
};

INSTANTIATE_TEST_SUITE_P(RequestCacheControlTest, RequestCacheControlTest,
                         testing::ValuesIn(RequestCacheControlTest::getTestCases()));

TEST_P(RequestCacheControlTest, RequestCacheControlTest) {
  const absl::string_view cache_control_header = GetParam().cache_control_header;
  const RequestCacheControl expected_request_cache_control = GetParam().request_cache_control;
  EXPECT_EQ(expected_request_cache_control, RequestCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(ResponseCacheControlTest, ResponseCacheControlTest,
                         testing::ValuesIn(ResponseCacheControlTest::getTestCases()));

TEST_P(ResponseCacheControlTest, ResponseCacheControlTest) {
  const absl::string_view cache_control_header = GetParam().cache_control_header;
  const ResponseCacheControl expected_response_cache_control = GetParam().response_cache_control;
  EXPECT_EQ(expected_response_cache_control, ResponseCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(HttpTimeTest::getOkTestCases()));

TEST_P(HttpTimeTest, Ok) {
  const Http::TestResponseHeaderMapImpl response_headers{{"date", GetParam()}};
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
