#include "extensions/filters/http/cache/cache_filter_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

struct IsCacheableRequestParams {
  Http::TestRequestHeaderMapImpl request_headers;
  bool is_cacheable;
};

IsCacheableRequestParams params[] = {
    {
        {},
        false
    },
    {
        {{":path", "/"}},
        false
    },
    {
        {{":path", "/"}, {":method", "GET"}},
        false
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "https"}},
        false
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "https"}, {":authority", "test.com"}},
        true
    },
    {
        {{":path", "/"}, {":method", "POST"}, {"x-forwarded-proto", "https"}, {":authority", "test.com"}},
        false
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "http"}, {":authority", "test.com"}},
        true
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "http"}, {":authority", "test.com"}},
        true
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "ftp"}, {":authority", "test.com"}},
        false
    },
    {
        {{":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "http"}, {":authority", "test.com"}, {"authorization", "basic YWxhZGRpbjpvcGVuc2VzYW1l"}},
        false
    },
};

class IsCacheableRequestTest : public testing::TestWithParam<IsCacheableRequestParams> {};

INSTANTIATE_TEST_SUITE_P(IsCacheableRequestTest, IsCacheableRequestTest, testing::ValuesIn(params));

TEST_P(IsCacheableRequestTest, IsCacheableRequestTest) { 
    EXPECT_EQ(CacheFilterUtils::isCacheableRequest(GetParam().request_headers), GetParam().is_cacheable);
}
    

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
