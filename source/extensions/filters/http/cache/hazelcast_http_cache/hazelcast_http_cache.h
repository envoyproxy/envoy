#pragma once

#include "common/common/logger.h"
#include "common/runtime/runtime_impl.h"

#include "source/extensions/filters/http/cache/hazelcast_http_cache/config.pb.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_storage_accessor.h"
#include "extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

// TODO(enozcan): Consider putting responses into cache with TTL derived from `max-age` header
//  instead of using a common TTL for all. This is possible during insertion by passing TTL
//  amount regardless of the configured TTL on Hazelcast server side.
//  i.e: IMap::put(const K &key, const V &value, int64_t ttlInMilliseconds);

using envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig;

/**
 * HttpCache implementation backed by Hazelcast.
 *
 * Supports two cache modes: UNIFIED and DIVIDED.
 *
 * In UNIFIED mode, an HTTP response is wrapped by a HazelcastResponseEntry
 * with its all fields (headers, body, trailers, request key) and stored in
 * distributed map. On a range HTTP request, regardless of the requested
 * range, the whole response body is fetched from the cache.
 *
 * In DIVIDED mode, an HTTP response's fields except for its body are wrapped
 * by a HazelcastHeaderEntry. Its body is divided into chunks with certain
 * sizes and then stored in another distributed map as HazelcastBodyEntry. On
 * a range request, not the whole body for the response but only the necessary
 * partitions are fetched from the cache. A header and its bodies have a common
 * number named <version> to interrelate multiple entries belong to the same response.
 *
 */
class HazelcastHttpCache : public HttpCache,
                           public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastHttpCache(HazelcastHttpCacheConfig config);

  /// Divided mode

  /**
   * Puts a header entry into header cache.
   * @param key     Hash key for the entry
   * @param entry   Entry to be inserted
   * @note          Generated keys should be consistent across restarts, architectures,
   *                builds, and configurations. Otherwise, different filters using the
   *                same Hazelcast cluster might store the same response with different
   *                keys.
   */
  void putHeader(const uint64_t key, const HazelcastHeaderEntry& entry) {
    accessor_->putHeader(mapKey(key), entry);
  }

  /**
   * Puts a body entry into body cache.
   * @param key     Hash key for the whole body derived from the header
   * @param order   Order of the body chunk among other partitions starting from 0
   * @param entry   Entry to be inserted
   * @note          The key for a body partition must be obtainable from its header key.
   */
  void putBody(const uint64_t key, const uint64_t order, const HazelcastBodyEntry& entry) {
    accessor_->putBody(orderedMapKey(key, order), entry);
  }

  /**
   * Performs a lookup to header cache for the given key.
   * @param key     Hash key for the entry
   * @return        HazelcastHeaderPtr to cached entry if found, nullptr otherwise
   */
  HazelcastHeaderPtr getHeader(const uint64_t key) { return accessor_->getHeader(mapKey(key)); }

  /**
   * Performs a lookup to body cache for the given key and order pair.
   * @param key     Hash key for the whole body
   * @param order   Order of the body chunk among other partitions
   * @return        HazelcastBodyPtr to cached entry if found, nullptr otherwise
   */
  HazelcastBodyPtr getBody(const uint64_t key, const uint64_t order) {
    return accessor_->getBody(orderedMapKey(key, order));
  }

  /**
   * Cleans up a malformed response when at least one of the body chunks are missed
   * during lookup. The header for the response is removed to make a new insertion
   * available by an insert context and the remaining body partitions are removed
   * to prevent orphan body entries stay in the cache.
   * @param key         Header key for the response
   * @param version     Version for the key and body
   * @param body_size   Total body size for the response
   */
  void onMissingBody(uint64_t key, int32_t version, uint64_t body_size);

  /**
   * Cleans up a malformed response when a body partition with different version
   * than the header is encountered during lookup.
   * @param key         Header key for the response
   * @param version     Version for the key and body
   * @param body_size   Total body size for the response
   */
  void onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size);

  /// Unified mode

  /**
   * Puts a unified entry into unified cache.
   * @param key     Hash key for the entry
   * @param entry   Entry to be inserted
   */
  void putResponse(const uint64_t key, const HazelcastResponseEntry& entry) {
    accessor_->putResponse(mapKey(key), entry);
  }

  /**
   * Performs a lookup to unified cache for the given key.
   * @param key     Hash key for the entry
   * @return        HazelcastResponsePtr to cached entry if found, nullptr otherwise
   */
  HazelcastResponsePtr getResponse(const uint64_t key) {
    return accessor_->getResponse(mapKey(key));
  }

  /// Common

  /**
   * Attempts to lock the given key in the cache. When a key is locked, a lookup
   * can be performed but an insertion or update for the key must be prevented
   * for threads other than the lock holder.
   * @param key     Key to be locked
   * @return        True if acquired, false otherwise
   * @note          Used to prevent multiple insertions or updates by different
   *                contexts at a time.
   */
  bool tryLock(const uint64_t key) { return accessor_->tryLock(mapKey(key), unified_); }

  /**
   * Releases the lock for the key.
   * @param     Key to be unlocked
   */
  void unlock(const uint64_t key) { accessor_->unlock(mapKey(key), unified_); }

  /**
   * Produces a random number.
   * @return    Random unsigned long
   * @note      The primary use case of the random number is to generate version
   *            for header and body entries in DIVIDED mode.
   */
  uint64_t random() { return rand_.random(); }

  /**
   * @return    Size in bytes for a single body entry configured for the cache
   * @note      Ignored in UNIFIED mode.
   */
  uint64_t bodySizePerEntry() { return body_partition_size_; }

  /**
   * @return    Allowed max size in bytes for a response configured for the cache
   * @note      Common for both modes. For a response which has a body larger
   *            than this limit, the first max_body_size_ bytes of the response
   *            will be cached only.
   */
  uint64_t maxBodySize() { return max_body_size_; }

  /**
   * Generates a unique signed key for an unsigned one.
   * @param unsigned_key    Unsigned hash key
   * @return                Signed unique key
   * @note                  Hazelcast client accepts signed map keys only.
   */
  inline int64_t mapKey(const uint64_t unsigned_key) {
    // The reason for not static casting directly is a possible overflow
    // for int64 on intermediate step for -2^63.
    int64_t signed_key;
    std::memcpy(&signed_key, &unsigned_key, sizeof(int64_t));
    return signed_key;
  }

  /**
   * Creates string keys for body partition entries obtainable from their header
   * keys.
   * @param key     Unsigned hash key for the header
   * @param order   Order of the body among other partitions starting from 0
   * @return        Body partition key unique for header and order pair
   * @note          Appending '#' or any other marker between the key and order
   *                string is required. Otherwise, for instance, the 11th order
   *                body for key 1 and the 1st order body for key 11 will have
   *                the same map key "111".
   */
  inline std::string orderedMapKey(const uint64_t key, const uint64_t order) {
    return std::to_string(key).append("#").append(std::to_string(order));
  }

  /**
   * Makes the cache ready to serve. Storage accessor connection must be established
   * via StorageAccessor#connect() when the cache is started.
   *
   * @note Keeping this virtual allows tests to override access strategy.
   * Using a local accessor will make the cache behavior testable without
   * starting a Hazelcast instance.
   */
  virtual void start();

  /**
   * Drops accessor connection to the storage.
   * @param destroy     True if accessor_ also should be destroyed.
   */
  void shutdown(bool destroy);

  // from Cache::HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(LookupContextPtr&& lookup_context,
                     Http::ResponseHeaderMapPtr&& response_headers) override;
  CacheInfo cacheInfo() const override;

  ~HazelcastHttpCache() override;

protected:
  /** Cache mode */
  const bool unified_;

  /** Partition size in bytes for a single body entry */
  const uint64_t body_partition_size_;

  /** Allowed max body size in bytes for a response */
  const uint64_t max_body_size_;

  /** Storage access */
  std::unique_ptr<StorageAccessor> accessor_;

  /** typed config from CacheConfig */
  HazelcastHttpCacheConfig cache_config_;

  Runtime::RandomGeneratorImpl rand_;
};

class HazelcastHttpCacheFactory : public HttpCacheFactory {
public:
  // UntypedFactory
  std::string name() const override;

  // TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  // HttpCacheFactory
  HttpCache&
  getCache(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) override;

  HttpCache& // For testing only.
  getOfflineCache(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config);

private:
  std::unique_ptr<HazelcastHttpCache> cache_;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
