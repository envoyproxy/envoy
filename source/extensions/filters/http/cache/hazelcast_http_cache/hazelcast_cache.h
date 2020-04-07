#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache_entry.h"
#include "extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Cache abstraction to support DIVIDED and UNIFIED cache modes.
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
class HazelcastCache : public HttpCache {
public:
  HazelcastCache(bool unified, uint64_t partition_size, uint64_t max_body_size)
      : unified_(unified), body_partition_size_(partition_size), max_body_size_(max_body_size) {}

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
  virtual void putHeader(const uint64_t key, const HazelcastHeaderEntry& entry) PURE;

  /**
   * Puts a body entry into body cache.
   * @note          The key for a body partition must be obtainable from its header key.
   * @param key     Hash key for the whole body
   * @param order   Order of the body chunk among other partitions
   * @param entry   Entry to be inserted
   */
  virtual void putBody(const uint64_t key, const uint64_t order,
                       const HazelcastBodyEntry& entry) PURE;

  /**
   * Performs a lookup to header cache for the given key.
   * @param key     Hash key for the entry
   * @return        HazelcastHeaderPtr to cached entry if found, nullptr otherwise
   */
  virtual HazelcastHeaderPtr getHeader(const uint64_t key) PURE;

  /**
   * Performs a lookup to body cache for the given key and order pair.
   * @param key     Hash key for the whole body
   * @param order   Order of the body chunk among other partitions
   * @return        HazelcastBodyPtr to cached entry if found, nullptr otherwise.
   */
  virtual HazelcastBodyPtr getBody(const uint64_t key, const uint64_t order) PURE;

  /**
   * Cleans up a malformed response when at least one of the body chunks are missed
   * during lookup. The header for the response is removed to make a new insertion
   * available by an insert context and the remaining body partitions are removed
   * to prevent orphan body entries stay in the cache.
   * @param key         Header key for the response
   * @param version     Version for the key and body
   * @param body_size   Total body size for the response
   */
  virtual void onMissingBody(uint64_t key, int32_t version, uint64_t body_size) PURE;

  /**
   * Cleans up a malformed response when a body partition with different version
   * than the header is encountered during lookup.
   * @param key         Header key for the response
   * @param version     Version for the key and body
   * @param body_size   Total body size for the response
   */
  virtual void onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size) PURE;

  /// Unified mode

  /**
   * Puts a unified entry into unified cache if no other entry associated with the key
   * is found.
   * @note          IfAbsent is to prevent race between multiple filters. Overriding
   *                an existing entry is forbidden. HttpCache::updateHeaders() should
   *                be used if changing the header content is necessary.
   * @param key     Hash key for the entry
   * @param entry   Entry to be inserted
   */
  virtual void putResponseIfAbsent(const uint64_t key, const HazelcastResponseEntry& entry) PURE;

  /**
   * Performs a lookup to unified cache for the given key.
   * @param key     Hash key for the entry.
   * @return        HazelcastResponsePtr to cached entry if found, nullptr otherwise.
   */
  virtual HazelcastResponsePtr getResponse(const uint64_t key) PURE;

  /// Common

  /**
   * Attempts to lock the given key in the cache. When a key is locked, a lookup
   * can be performed but an insertion or update for the key must be prevented.
   * @note          Used to prevent multiple insertions or updates by different
   *                contexts at a time.
   * @param key     Key to be locked.
   * @return        True if acquired, false otherwise.
   */
  virtual bool tryLock(const uint64_t key) PURE;

  /**
   * Releases the lock for the key.
   * @param     Key to be unlocked
   */
  virtual void unlock(const uint64_t key) PURE;

  /**
   * Produces a random number.
   * @return    Random unsigned long.
   * @note      The primary use case for the random number is to generate version
   *            for header and body entries.
   */
  virtual uint64_t random() PURE;

  /**
   * @note      Ignored in UNIFIED mode.
   * @return    Size in bytes for a single body entry configured for the cache
   */
  uint64_t bodySizePerEntry() { return body_partition_size_; };

  /**
   * @return    Allowed max size in bytes for a response configured for the cache
   * @note      Common for both modes. For a response which has a body larger
   *            than this limit, the first max_body_size_ bytes of the response
   *            will be cached only.
   */
  uint64_t maxBodySize() { return max_body_size_; };

  /**
   * Generates a unique signed key for an unsigned one.
   * @param unsigned_key    Unsigned hash key
   * @return                Signed unique key
   * @note                  Hazelcast client accepts signed keys only.
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
   *
   * @note          Appending '#' or any other marker between the key and order
   *                string is required. Otherwise, for instance, the 11th order
   *                body for key 1 and the 1st order body for key 11 will have
   *                the same map key "111".
   */
  inline std::string orderedMapKey(const uint64_t key, const uint64_t order) {
    return std::to_string(key).append("#").append(std::to_string(order));
  }

  virtual ~HazelcastCache() = default;

protected:
  /** Cache mode */
  const bool unified_;

  /** Partition size in bytes for a single body entry */
  const uint64_t body_partition_size_;

  /** Allowed max size in bytes for a response */
  const uint64_t max_body_size_;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
