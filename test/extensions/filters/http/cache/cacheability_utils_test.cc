#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/cache/cacheability_utils.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CanServeRequestFromCacheTest : public testing::Test {
protected:
  Http::TestRequestHeaderMapImpl request_headers_ = {
      {":path", "/"}, {":method", "GET"}, {":scheme", "http"}, {":authority", "test.com"}};
};

class RequestConditionalHeadersTest : public testing::TestWithParam<std::string> {
protected:
  Http::TestRequestHeaderMapImpl request_headers_ = {
      {":path", "/"}, {":method", "GET"}, {":scheme", "http"}, {":authority", "test.com"}};
  std::string conditionalHeader() const { return GetParam(); }
};

envoy::extensions::filters::http::cache::v3::CacheConfig getConfig() {
  // Allows 'accept' to be varied in the tests.
  envoy::extensions::filters::http::cache::v3::CacheConfig config;
  const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
  add_accept->set_exact("accept");
  return config;
}

class IsCacheableResponseTest : public testing::Test {
public:
  IsCacheableResponseTest()
      : vary_allow_list_(getConfig().allowed_vary_headers(), factory_context_) {}

protected:
  std::string cache_control_ = "max-age=3600";
  Http::TestResponseHeaderMapImpl response_headers_ = {{":status", "200"},
                                                       {"date", "Sun, 06 Nov 1994 08:49:37 GMT"},
                                                       {"cache-control", cache_control_}};

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  VaryAllowList vary_allow_list_;
};

TEST_F(CanServeRequestFromCacheTest, CacheableRequest) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(CanServeRequestFromCacheTest, PathHeader) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.removePath();
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(CanServeRequestFromCacheTest, HostHeader) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.removeHost();
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(CanServeRequestFromCacheTest, MethodHeader) {
  const Http::HeaderValues& header_values = Http::Headers::get();
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setMethod(header_values.MethodValues.Post);
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setMethod(header_values.MethodValues.Put);
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setMethod(header_values.MethodValues.Head);
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.removeMethod();
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(CanServeRequestFromCacheTest, SchemeHeader) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setScheme("ftp");
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.removeScheme();
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(CanServeRequestFromCacheTest, AuthorizationHeader) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Authorization,
                                   "basic YWxhZGRpbjpvcGVuc2VzYW1l");
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

INSTANTIATE_TEST_SUITE_P(ConditionalHeaders, RequestConditionalHeadersTest,
                         testing::Values("if-none-match", "if-modified-since", "if-range"),
                         [](const auto& info) {
                           std::string test_name = info.param;
                           absl::c_replace_if(
                               test_name, [](char c) { return !std::isalnum(c); }, '_');
                           return test_name;
                         });

TEST_P(RequestConditionalHeadersTest, ConditionalHeaders) {
  EXPECT_TRUE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
  request_headers_.setCopy(Http::LowerCaseString{conditionalHeader()}, "test-value");
  EXPECT_FALSE(CacheabilityUtils::canServeRequestFromCache(request_headers_));
}

TEST_F(IsCacheableResponseTest, CacheableResponse) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, UncacheableStatusCode) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  response_headers_.setStatus("700");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  response_headers_.removeStatus();
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, ValidationData) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  // No cache control headers or expires header
  response_headers_.remove(Http::CustomHeaders::get().CacheControl);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  // No max-age data or expires header
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    "public, no-transform");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  // Max-age data available
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "s-maxage=1000");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  // No max-age data, but the response requires revalidation anyway
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  // No cache control headers, but there is an expires header
  response_headers_.remove(Http::CustomHeaders::get().CacheControl);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Expires,
                                    "Sun, 06 Nov 1994 09:49:37 GMT");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, ResponseNoStore) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  std::string cache_control_no_store = absl::StrCat(cache_control_, ", no-store");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                    cache_control_no_store);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, ResponsePrivate) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  std::string cache_control_private = absl::StrCat(cache_control_, ", private");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, cache_control_private);
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, EmptyVary) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  response_headers_.setCopy(Http::CustomHeaders::get().Vary, "");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, AllowedVary) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  response_headers_.setCopy(Http::CustomHeaders::get().Vary, "accept");
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

TEST_F(IsCacheableResponseTest, NotAllowedVary) {
  EXPECT_TRUE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
  response_headers_.setCopy(Http::CustomHeaders::get().Vary, "*");
  EXPECT_FALSE(CacheabilityUtils::isCacheableResponse(response_headers_, vary_allow_list_));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
