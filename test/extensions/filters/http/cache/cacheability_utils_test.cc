#include "envoy/http/header_map.h"

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
  Http::TestRequestHeaderMapImpl request_headers_ = {{":path", "/"},
                                                     {":method", "GET"},
                                                     {"x-forwarded-proto", "http"},
                                                     {":authority", "test.com"}};
};

class RequestConditionalHeadersTest : public testing::TestWithParam<std::string> {
protected:
  Http::TestRequestHeaderMapImpl request_headers_ = {{":path", "/"},
                                                     {":method", "GET"},
                                                     {"x-forwarded-proto", "http"},
                                                     {":authority", "test.com"}};
  std::string conditionalHeader() const { return GetParam(); }
};

class IsCacheableResponseTest : public testing::Test {
protected:
  std::string cache_control_ = "max-age=3600";
  Http::TestResponseHeaderMapImpl response_headers_ = {{":status", "200"},
                                                       {"date", "Sun, 06 Nov 1994 08:49:37 GMT"},
                                                       {"cache-control", cache_control_}};
};

TEST_F(IsCacheableRequestTest, CacheableRequest) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableRequestTest, PathHeader) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.removePath();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableRequestTest, HostHeader) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.removeHost();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableRequestTest, MethodHeader) {
  const Http::HeaderValues& header_values = Http::Headers::get();
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.setMethod(header_values.MethodValues.Post);
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.setMethod(header_values.MethodValues.Put);
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.removeMethod();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableRequestTest, ForwardedProtoHeader) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.setForwardedProto("ftp");
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.removeForwardedProto();
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableRequestTest, AuthorizationHeader) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Authorization,
                                   "basic YWxhZGRpbjpvcGVuc2VzYW1l");
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

INSTANTIATE_TEST_SUITE_P(ConditionalHeaders, RequestConditionalHeadersTest,
                         testing::Values("if-match", "if-none-match", "if-modified-since",
                                         "if-unmodified-since", "if-range"),
                         [](const auto& info) {
                           std::string test_name = info.param;
                           absl::c_replace_if(
                               test_name, [](char c) { return !std::isalnum(c); }, '_');
                           return test_name;
                         });

TEST_P(RequestConditionalHeadersTest, ConditionalHeaders) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableRequest(request_headers_));
  request_headers_.setCopy(Http::LowerCaseString{conditionalHeader()}, "test-value");
  EXPECT_FALSE(CacheabilityUtils::isCacheableRequest(request_headers_));
}

TEST_F(IsCacheableResponseTest, CacheableResponse) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
}

TEST_F(IsCacheableResponseTest, UncacheableStatusCode) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  response_headers_.setStatus("700");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
  response_headers_.removeStatus();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
}

TEST_F(IsCacheableResponseTest, ValidationData) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // No cache control headers or expires header
  response_headers_.remove(Http::CustomHeaders::get().CacheControl);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // No max-age data or expires header
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    "public, no-transform");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // Max-age data available
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "s-maxage=1000");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // Max-age data available with no date
  response_headers_.removeDate();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // No date, but the response requires revalidation anyway
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  // No cache control headers or date, but there is an expires header
  response_headers_.setReferenceKey(Http::Headers::get().Expires, "Sun, 06 Nov 1994 09:49:37 GMT");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
}

TEST_F(IsCacheableResponseTest, ResponseNoStore) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  std::string cache_control_no_store = absl::StrCat(cache_control_, ", no-store");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    cache_control_no_store);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
}

TEST_F(IsCacheableResponseTest, ResponsePrivate) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_));
  std::string cache_control_private = absl::StrCat(cache_control_, ", private");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, cache_control_private);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
