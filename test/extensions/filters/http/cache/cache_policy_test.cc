#include <cctype>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/cache/simple_http_cache/v3/config.pb.h"
#include "envoy/extensions/filters/http/cache/v3/cache_policy.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/cache_policy.h"

#include "test/test_common/utility.h"

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CachePolicyImplTest : public testing::TestWithParam<std::string> {
public:
  CachePolicyImplTest() : vary_allow_list_(getConfig().allowed_vary_headers()) {}

protected:
  std::string conditionalHeader() const { return GetParam(); }

  ::envoy::extensions::filters::http::cache::v3::CacheConfig getConfig() {
    // Allows 'accept' to be varied in the tests.
    ::envoy::extensions::filters::http::cache::v3::CacheConfig config;
    const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
    add_accept->set_exact("accept");
    return config;
  }

  std::string cache_control_ = "max-age=3600";
  Envoy::Http::TestResponseHeaderMapImpl response_headers_ = {
      {":status", "200"},
      {"date", "Sun, 06 Nov 1994 08:49:37 GMT"},
      {"cache-control", cache_control_}};
  Envoy::Http::TestRequestHeaderMapImpl request_headers_ = {{":path", "/"},
                                                            {":method", "GET"},
                                                            {"x-forwarded-proto", "http"},
                                                            {":authority", "test.com"}};

  const RequestCacheControl request_cache_control() {
    return RequestCacheControl(request_headers_);
  }

  const ResponseCacheControl response_cache_control() {
    absl::string_view cache_control =
        response_headers_.getInlineValue(CacheCustomHeaders::responseCacheControl());
    return ResponseCacheControl(cache_control);
  }

  VaryAllowList vary_allow_list_;
  CachePolicyImpl policy_;
};

TEST_F(CachePolicyImplTest, CacheableRequest) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, PathHeader) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.removePath();
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, HostHeader) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.removeHost();
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, MethodHeader) {
  const Envoy::Http::HeaderValues& header_values = Envoy::Http::Headers::get();
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.setMethod(header_values.MethodValues.Post);
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.setMethod(header_values.MethodValues.Put);
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.removeMethod();
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, ForwardedProtoHeader) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.setForwardedProto("ftp");
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.removeForwardedProto();
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, AuthorizationHeader) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().Authorization,
                                   "basic YWxhZGRpbjpvcGVuc2VzYW1l");
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, RequestNoStore) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  std::string cache_control_no_store = absl::StrCat(cache_control_, ", no-store");
  request_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl,
                                   cache_control_no_store);
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

INSTANTIATE_TEST_SUITE_P(ConditionalHeaders, CachePolicyImplTest,
                         testing::Values("if-match", "if-none-match", "if-modified-since",
                                         "if-unmodified-since", "if-range"),
                         [](const auto& info) {
                           std::string test_name = info.param;
                           absl::c_replace_if(
                               test_name, [](char c) { return !std::isalnum(c); }, '_');
                           return test_name;
                         });

TEST_P(CachePolicyImplTest, ConditionalHeaders) {
  EXPECT_TRUE(policy_.requestCacheable(request_headers_, request_cache_control()));
  request_headers_.setCopy(Envoy::Http::LowerCaseString{conditionalHeader()}, "test-value");
  EXPECT_FALSE(policy_.requestCacheable(request_headers_, request_cache_control()));
}

TEST_F(CachePolicyImplTest, CacheableResponse) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, UncacheableStatusCode) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  response_headers_.setStatus("700");
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
  response_headers_.removeStatus();
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, ValidationData) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  // No cache control headers or expires header
  response_headers_.remove(Envoy::Http::CustomHeaders::get().CacheControl);
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
  // No max-age data or expires header
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl,
                                    "public, no-transform");
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
  // Max-age data available
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl,
                                    "s-maxage=1000");
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  // No max-age data, but the response requires revalidation anyway
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl, "no-cache");
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  // No cache control headers, but there is an expires header
  response_headers_.remove(Envoy::Http::CustomHeaders::get().CacheControl);
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().Expires,
                                    "Sun, 06 Nov 1994 09:49:37 GMT");
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, ResponseNoStore) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  std::string cache_control_no_store = absl::StrCat(cache_control_, ", no-store");
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl,
                                    cache_control_no_store);
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, ResponsePrivate) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  std::string cache_control_private = absl::StrCat(cache_control_, ", private");
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl,
                                    cache_control_private);
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, EmptyVary) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  response_headers_.setCopy(Envoy::Http::CustomHeaders::get().Vary, "");
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, AllowedVary) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  response_headers_.setCopy(Envoy::Http::CustomHeaders::get().Vary, "accept");
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
}

TEST_F(CachePolicyImplTest, NotAllowedVary) {
  EXPECT_TRUE(policy_.responseCacheable(request_headers_, response_headers_,
                                        response_cache_control(), vary_allow_list_));
  response_headers_.setCopy(Envoy::Http::CustomHeaders::get().Vary, "*");
  EXPECT_FALSE(policy_.responseCacheable(request_headers_, response_headers_,
                                         response_cache_control(), vary_allow_list_));
}

TEST(CacheEntryUsabilityTest, IsEqualTrue) {
  EXPECT_EQ((CacheEntryUsability{CacheEntryStatus::Ok, Envoy::Seconds(0)}),
            (CacheEntryUsability{CacheEntryStatus::Ok, Envoy::Seconds(0)}));
}

TEST(CacheEntryUsabilityTest, IsEqualFalseStatus) {
  EXPECT_NE((CacheEntryUsability{CacheEntryStatus::Unusable, Envoy::Seconds(0)}),
            (CacheEntryUsability{CacheEntryStatus::Ok, Envoy::Seconds(0)}));
}

TEST(CacheEntryUsabilityTest, IsEqualFalseAge) {
  EXPECT_NE((CacheEntryUsability{CacheEntryStatus::Ok, Envoy::Seconds(1)}),
            (CacheEntryUsability{CacheEntryStatus::Ok, Envoy::Seconds(0)}));
}

// TODO(kiehl): test computeCacheEntryUsability independently as it's currently
// only covered indirectly by other tests.

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
