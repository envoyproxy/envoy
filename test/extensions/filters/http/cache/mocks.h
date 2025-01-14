#pragma once

#include "source/extensions/filters/http/cache/active_cache.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/cache/http_source.h"
#include "source/extensions/filters/http/cache/stats.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

void PrintTo(const EndStream& end_stream, std::ostream* os);
void PrintTo(const Key& key, std::ostream* os);

class MockActiveCache : public ActiveCache {
public:
  MOCK_METHOD(void, lookup, (ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb));
  MOCK_METHOD(CacheInfo, cacheInfo, (), (const));
  MOCK_METHOD(HttpCache&, cache, (), (const));
};

class MockHttpCache : public HttpCache {
public:
  MOCK_METHOD(void, lookup, (LookupRequest && request, LookupCallback&& callback));
  MOCK_METHOD(void, evict, (Event::Dispatcher & dispatcher, const Key& key));
  MOCK_METHOD(void, touch, (const Key& key, SystemTime timestamp));
  MOCK_METHOD(void, updateHeaders,
              (Event::Dispatcher & dispatcher, const Key& key,
               const Http::ResponseHeaderMap& updated_headers,
               const ResponseMetadata& updated_metadata));
  MOCK_METHOD(CacheInfo, cacheInfo, (), (const));
  MOCK_METHOD(void, insert,
              (Event::Dispatcher & dispatcher, Key key, Http::ResponseHeaderMapPtr headers,
               ResponseMetadata metadata, HttpSourcePtr source,
               std::shared_ptr<CacheProgressReceiver> progress));
};

class MockCacheReader : public CacheReader {
public:
  MOCK_METHOD(void, getBody,
              (Event::Dispatcher & dispatcher, AdjustedByteRange range, GetBodyCallback&& cb));
};

class MockHttpSource : public HttpSource {
public:
  MOCK_METHOD(void, getHeaders, (GetHeadersCallback && cb));
  MOCK_METHOD(void, getBody, (AdjustedByteRange range, GetBodyCallback&& cb));
  MOCK_METHOD(void, getTrailers, (GetTrailersCallback && cb));
};

class MockCacheFilterStats : public CacheFilterStats {
public:
  MOCK_METHOD(void, incForStatus, (CacheEntryStatus s));
};

class FakeStreamHttpSource : public HttpSource {
public:
  // Any field can be nullptr; if headers is nullptr it's assumed headers have
  // already been consumed. Body and trailers being nullptr imply the resource had
  // no body or trailers respectively.
  FakeStreamHttpSource(Event::Dispatcher& dispatcher, Http::ResponseHeaderMapPtr headers,
                       absl::string_view body, Http::ResponseTrailerMapPtr trailers);
  void getHeaders(GetHeadersCallback&& cb) override;
  // This will use the dispatcher, to better resemble the behavior of an actual
  // async http stream.
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override;
  void getTrailers(GetTrailersCallback&& cb) override;
  void setMaxFragmentSize(uint64_t v) { max_fragment_size_ = v; }

private:
  Event::Dispatcher& dispatcher_;
  Http::ResponseHeaderMapPtr headers_;
  std::string body_;
  Http::ResponseTrailerMapPtr trailers_;
  uint64_t body_pos_{0};
  uint64_t max_fragment_size_ = std::numeric_limits<uint64_t>::max();
};

class MockCacheProgressReceiver : public CacheProgressReceiver {
public:
  MOCK_METHOD(void, onHeadersInserted,
              (CacheReaderPtr cache_reader, Http::ResponseHeaderMapPtr headers, bool end_stream));
  MOCK_METHOD(void, onBodyInserted, (AdjustedByteRange range, bool end_stream));
  MOCK_METHOD(void, onTrailersInserted, (Http::ResponseTrailerMapPtr trailers));
  MOCK_METHOD(void, onInsertFailed, ());
};

class MockHttpCacheFactory : public HttpCacheFactory {
public:
  MOCK_METHOD(std::shared_ptr<ActiveCache>, getCache,
              (const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
               Server::Configuration::ServerFactoryContext& context));
};

class MockUpstreamRequestFactory : public UpstreamRequestFactory {
public:
  MOCK_METHOD(HttpSourcePtr, create, (Http::RequestHeaderMap & headers));
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
