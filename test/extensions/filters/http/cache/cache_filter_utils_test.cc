#include "extensions/filters/http/cache/cache_filter_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

Http::TestRequestHeaderMapImpl non_cacheable_request_headers[] = {
    {},
    {{":path", "/"}},
    {{":path", "/"}, {":method", "GET"}},
    {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "https"}},
    {{":path", "/"},
     {":method", "POST"},
     {"x-forwarded-proto", "https"},
     {":authority", "test.com"}},
    {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "ftp"}, {":authority", "test.com"}},
    {{":path", "/"},
     {":method", "GET"},
     {"x-forwarded-proto", "http"},
     {":authority", "test.com"},
     {"authorization", "basic YWxhZGRpbjpvcGVuc2VzYW1l"}},
};

Http::TestRequestHeaderMapImpl cacheable_request_headers[] = {
    {{":path", "/"},
     {":method", "GET"},
     {"x-forwarded-proto", "https"},
     {":authority", "test.com"}},
    {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "http"}, {":authority", "test.com"}},
    {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "http"}, {":authority", "test.com"}},
};

class NonCacheableRequestsTest : public testing::TestWithParam<Http::TestRequestHeaderMapImpl> {};
class CacheableRequestsTest : public testing::TestWithParam<Http::TestRequestHeaderMapImpl> {};

INSTANTIATE_TEST_SUITE_P(NonCacheableRequestsTest, NonCacheableRequestsTest,
                         testing::ValuesIn(non_cacheable_request_headers));
INSTANTIATE_TEST_SUITE_P(CacheableRequestsTest, CacheableRequestsTest,
                         testing::ValuesIn(cacheable_request_headers));

TEST_P(NonCacheableRequestsTest, NonCacheableRequests) {
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(GetParam()));
}

TEST_P(CacheableRequestsTest, CacheableRequests) {
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(GetParam()));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
