#include "source/extensions/filters/http/cache/cache_custom_headers.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

using RequestHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<
    Http::CustomInlineHeaderRegistry::Type::RequestHeaders>;
using ResponseHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<
    Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>;

TEST(CacheCustomHeadersTest, EnsureCacheCustomHeadersGettersDoNotFail) {
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"},
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "example.com"},
      {"authorization", "Basic abc123def456"},
      {"pragma", "no-cache"},
      {"cache-control", "no-store"},
      {"if-match", "abc123"},
      {"if-none-match", "def456"},
      {"if-modified-since", "16 Oct 2021 07:00:00 GMT"},
      {"if-unmodified-since", "28 Feb 2021 13:00:00 GMT"},
      {"if-range", "ghi789"}};

  // Ensure we can retrieve each custom header without failure.
  const Http::HeaderEntry* authorization =
      request_headers_.getInline(CacheCustomHeaders::authorization());
  ASSERT_EQ(authorization->value().getStringView(), "Basic abc123def456");
  const Http::HeaderEntry* pragma = request_headers_.getInline(CacheCustomHeaders::pragma());
  ASSERT_EQ(pragma->value().getStringView(), "no-cache");
  const Http::HeaderEntry* request_cache_control =
      request_headers_.getInline(CacheCustomHeaders::requestCacheControl());
  ASSERT_EQ(request_cache_control->value().getStringView(), "no-store");
  const Http::HeaderEntry* if_match = request_headers_.getInline(CacheCustomHeaders::ifMatch());
  ASSERT_EQ(if_match->value().getStringView(), "abc123");
  const Http::HeaderEntry* if_none_match =
      request_headers_.getInline(CacheCustomHeaders::ifNoneMatch());
  ASSERT_EQ(if_none_match->value().getStringView(), "def456");
  const Http::HeaderEntry* if_modified_since =
      request_headers_.getInline(CacheCustomHeaders::ifModifiedSince());
  ASSERT_EQ(if_modified_since->value().getStringView(), "16 Oct 2021 07:00:00 GMT");
  const Http::HeaderEntry* if_unmodified_since =
      request_headers_.getInline(CacheCustomHeaders::ifUnmodifiedSince());
  ASSERT_EQ(if_unmodified_since->value().getStringView(), "28 Feb 2021 13:00:00 GMT");
  const Http::HeaderEntry* if_range = request_headers_.getInline(CacheCustomHeaders::ifRange());
  ASSERT_EQ(if_range->value().getStringView(), "ghi789");

  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"cache-control", "public,max-age=3600"},
                                                    {"last-modified", "27 Sept 2021 04:00:00 GMT"},
                                                    {"age", "123"},
                                                    {"etag", "abc123"},
                                                    {"expires", "01 Jan 2021 00:00:00 GMT"}};

  const Http::HeaderEntry* response_cache_control =
      response_headers_.getInline(CacheCustomHeaders::responseCacheControl());
  ASSERT_EQ(response_cache_control->value().getStringView(), "public,max-age=3600");
  const Http::HeaderEntry* last_modified =
      response_headers_.getInline(CacheCustomHeaders::lastModified());
  ASSERT_EQ(last_modified->value().getStringView(), "27 Sept 2021 04:00:00 GMT");
  const Http::HeaderEntry* age = response_headers_.getInline(CacheCustomHeaders::age());
  ASSERT_EQ(age->value().getStringView(), "123");
  const Http::HeaderEntry* etag = response_headers_.getInline(CacheCustomHeaders::etag());
  ASSERT_EQ(etag->value().getStringView(), "abc123");
  const Http::HeaderEntry* expires = response_headers_.getInline(CacheCustomHeaders::expires());
  ASSERT_EQ(expires->value().getStringView(), "01 Jan 2021 00:00:00 GMT");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
