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
  std::vector<Matchers::StringMatcherPtr> vary_allowlist;
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
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, UncacheableStatusCode) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  response_headers_.setStatus("700");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  response_headers_.removeStatus();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, ValidationData) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // No cache control headers or expires header
  response_headers_.remove(Http::CustomHeaders::get().CacheControl);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // No max-age data or expires header
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    "public, no-transform");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // Max-age data available
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "s-maxage=1000");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // Max-age data available with no date
  response_headers_.removeDate();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // No date, but the response requires revalidation anyway
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  // No cache control headers or date, but there is an expires header
  response_headers_.setReferenceKey(Http::Headers::get().Expires, "Sun, 06 Nov 1994 09:49:37 GMT");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, ResponseNoStore) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  std::string cache_control_no_store = absl::StrCat(cache_control_, ", no-store");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    cache_control_no_store);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, ResponsePrivate) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  std::string cache_control_private = absl::StrCat(cache_control_, ", private");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, cache_control_private);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, EmptyVary) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  response_headers_.setCopy(Http::Headers::get().Vary, "");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, AllowedVary) {
  // Sets "accept" as the only allowed header to be varied upon.
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("accept");
  vary_allowlist.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  response_headers_.setCopy(Http::Headers::get().Vary, "accept");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

TEST_F(IsCacheableResponseTest, NotAllowedVary) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
  response_headers_.setCopy(Http::Headers::get().Vary, "*");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allowlist));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
