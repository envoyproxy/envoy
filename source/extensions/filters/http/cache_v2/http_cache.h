#pragma once

#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/cache_v2/v3/cache.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_progress_receiver.h"
#include "source/extensions/filters/http/cache_v2/http_source.h"
#include "source/extensions/filters/http/cache_v2/key.pb.h"
#include "source/extensions/filters/http/cache_v2/range_utils.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheSessions;
class CacheReader;

// Result of a lookup operation.
struct LookupResult {
  std::unique_ptr<CacheReader> cache_reader_;
  std::unique_ptr<Http::ResponseHeaderMap> response_headers_;
  std::unique_ptr<Http::ResponseTrailerMap> response_trailers_;
  ResponseMetadata response_metadata_;
  absl::optional<uint64_t> body_length_;
  bool populated() const { return body_length_.has_value(); }
};

// Produces a hash of key that is consistent across restarts, architectures,
// builds, and configurations. Caches that store persistent entries based on a
// 64-bit hash should (but are not required to) use stableHashKey. Once this API
// leaves alpha, any improvements to stableHashKey that would change its output
// for existing callers is a breaking change.
//
// For non-persistent storage, use MessageUtil, which has no long-term stability
// guarantees.
//
// When providing a cached response, Caches must ensure that the keys (and not
// just their hashes) match.
size_t stableHashKey(const Key& key);

// LookupRequest holds everything about a request that's needed to look for a
// response in a cache, to evaluate whether an entry from a cache is usable, and
// to determine what ranges are needed.
class LookupRequest {
public:
  // Prereq: request_headers's Path(), Scheme(), and Host() are non-null.
  LookupRequest(Key&& key, Event::Dispatcher& dispatcher);

  // Caches may modify the key according to local needs, though care must be
  // taken to ensure that meaningfully distinct responses have distinct keys.
  const Key& key() const { return key_; }

  Event::Dispatcher& dispatcher() const { return dispatcher_; }

private:
  Event::Dispatcher& dispatcher_;
  Key key_;
};

// Statically known information about a cache.
struct CacheInfo {
  absl::string_view name_;
};

class CacheReader {
public:
  // May call the callback immediately; dispatcher is provided as an option to facilitate
  // asynchronous operations.
  // Will only be called with ranges the cache has announced are available, either via
  // CacheProgressReceiver::onBodyInserted or via HttpCache::LookupCallback.
  // end_stream should always be More, unless a cache error occurs in which case Reset -
  // client already knows the body length so cache does not need to detect 'End'.
  virtual void getBody(Event::Dispatcher& dispatcher, AdjustedByteRange range,
                       GetBodyCallback&& cb) PURE;
  virtual ~CacheReader() = default;
};
using CacheReaderPtr = std::unique_ptr<CacheReader>;

// Implement this interface to provide a cache implementation for use by
// CacheFilter.
class HttpCache {
public:
  // LookupCallback returns an empty LookupResult if the cache entry does not exist.
  // Statuses are for actual errors.
  using LookupCallback = absl::AnyInvocable<void(absl::StatusOr<LookupResult>&&)>;

  // Returns statically known information about a cache.
  virtual CacheInfo cacheInfo() const PURE;

  // Calls the callback with a LookupResult; its body_length_ should be nullopt
  // if the key was not found in the cache. Its cache_reader may be nullopt if the
  // cache entry has no body.
  // Using the dispatcher is optional, the callback is thread-safe.
  // The callback must be called - if the cache is deleted while a callback
  // is still in flight, the callback should be called with an error status.
  virtual void lookup(LookupRequest&& request, LookupCallback&& callback) PURE;

  // Remove the entry from the cache.
  // This should accept any dispatcher, as the cache has no worker affinity.
  virtual void evict(Event::Dispatcher& dispatcher, const Key& key) PURE;

  // To facilitate LRU cache eviction, provide a timestamp whenever a cache entry is
  // looked up.
  virtual void touch(const Key& key, SystemTime timestamp) PURE;

  // Replaces the headers in the cache.
  // If this requires asynchronous operations, getBody must continue to function for the duration
  // (perhaps reading from the existing data).
  // This should avoid modifying the data in-place non-atomically, as during hot restart or other
  // circumstances in which multiple instances are accessing the same cache, the data store could
  // be read from while partially written.
  // If the key doesn't exist, this should be a no-op.
  virtual void updateHeaders(Event::Dispatcher& dispatcher, const Key& key,
                             const Http::ResponseHeaderMap& updated_headers,
                             const ResponseMetadata& updated_metadata) PURE;

  // insert is only called after the headers have been read successfully and confirmed
  // to be cacheable, so the headers are provided immediately as the HttpSource has
  // already consumed them.
  // If end_stream was true, HttpSourcePtr is null.
  // The cache insert for future lookup() should only be completed atomically when the
  // insertion is finished, while the CacheReader passed to progress->onHeadersInserted
  // should be ready for streaming from immediately (subject to relevant body progress).
  virtual void insert(Event::Dispatcher& dispatcher, Key key, Http::ResponseHeaderMapPtr headers,
                      ResponseMetadata metadata, HttpSourcePtr source,
                      std::shared_ptr<CacheProgressReceiver> progress) PURE;
  virtual ~HttpCache() = default;
};

// Factory interface for cache implementations to implement and register.
class HttpCacheFactory : public Config::TypedFactory {
public:
  // From UntypedFactory
  std::string category() const override { return "envoy.http.cache_v2"; }

  // Returns a CacheSessions initialized with an HttpCache that will remain
  // valid indefinitely (at least as long as the calling CacheFilter).
  //
  // Pass factory context to allow HttpCache to use async client, stats scope
  // etc.
  virtual absl::StatusOr<std::shared_ptr<CacheSessions>>
  getCache(const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
           Server::Configuration::FactoryContext& context) PURE;

private:
  const std::string name_;
};
using HttpCacheFactoryPtr = std::unique_ptr<HttpCacheFactory>;
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
