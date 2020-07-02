#include "extensions/filters/http/cache/cache_filter_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class IsCacheableRequestTest : public testing::Test {
protected:
  const Http::TestRequestHeaderMapImpl cacheable_request_headers = {{":path", "/"},
                                                                    {":method", "GET"},
                                                                    {"x-forwarded-proto", "http"},
                                                                    {":authority", "test.com"}};
};

TEST_F(IsCacheableRequestTest, PathHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.removePath();
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, HostHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.removeHost();
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, MethodHeader) {
  const Http::HeaderValues& header_values = Http::Headers::get();
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.setMethod(header_values.MethodValues.Post);
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.setMethod(header_values.MethodValues.Put);
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.removeMethod();
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, ForwardedProtoHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.setForwardedProto("ftp");
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.removeForwardedProto();
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, AuthorizationHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheFilterUtils::isCacheableRequest(request_headers));
  request_headers.setCopy(Http::CustomHeaders::get().Authorization,
                          "basic YWxhZGRpbjpvcGVuc2VzYW1l");
  EXPECT_FALSE(CacheFilterUtils::isCacheableRequest(request_headers));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
