#include "extensions/filters/http/cache/cacheability_utils.h"

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

class IsCacheableResponseTest : public testing::Test {
protected:
  const Http::TestResponseHeaderMapImpl cacheable_response_headers = {
      {":status", "200"},
      {"date", "Sun, 06 Nov 1994 08:49:37 GMT"},
      {"cache-control", "max-age=3600"}};
};

TEST_F(IsCacheableRequestTest, CacheableRequest) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(cacheable_request_headers));
}

TEST_F(IsCacheableRequestTest, PathHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.removePath();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, HostHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.removeHost();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, MethodHeader) {
  const Http::HeaderValues& header_values = Http::Headers::get();
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.setMethod(header_values.MethodValues.Post);
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.setMethod(header_values.MethodValues.Put);
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.removeMethod();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, ForwardedProtoHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.setForwardedProto("ftp");
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.removeForwardedProto();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableRequestTest, AuthorizationHeader) {
  Http::TestRequestHeaderMapImpl request_headers = cacheable_request_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers));
  request_headers.setAuthorization("basic YWxhZGRpbjpvcGVuc2VzYW1l");
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers));
}

TEST_F(IsCacheableResponseTest, CacheableResponse) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(cacheable_response_headers));
}

TEST_F(IsCacheableResponseTest, UncacheableStatusCode) {
  Http::TestResponseHeaderMapImpl response_headers = cacheable_response_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.setStatus("700");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.removeStatus();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
}

TEST_F(IsCacheableResponseTest, ValidationData) {
  Http::TestResponseHeaderMapImpl response_headers = cacheable_response_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.setCacheControl("s-maxage=1000");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.setCacheControl("public, no-transform");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.removeCacheControl();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
  response_headers.setCopy(Http::Headers::get().Expires, "Sun, 06 Nov 1994 09:49:37 GMT");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
}

TEST_F(IsCacheableResponseTest, ResponseNoStore) {
  Http::TestResponseHeaderMapImpl response_headers = cacheable_response_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
  absl::string_view cache_control = response_headers.getCacheControlValue();
  std::string cache_control_no_store = absl::StrCat(cache_control, ", no-store");
  response_headers.setCacheControl(cache_control_no_store);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
}

TEST_F(IsCacheableResponseTest, ResponsePrivate) {
  Http::TestResponseHeaderMapImpl response_headers = cacheable_response_headers;
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers));
  absl::string_view cache_control = response_headers.getCacheControlValue();
  std::string cache_control_private = absl::StrCat(cache_control, ", private");
  response_headers.setCacheControl(cache_control_private);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
