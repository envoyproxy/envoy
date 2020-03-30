#pragma once

#include "common/common/logger.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache_entry.h"

#include "hazelcast/client/HazelcastClient.h"
#include "hazelcast/client/IMap.h"

#include "source/extensions/filters/http/cache/hazelcast_http_cache/config.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using hazelcast::client::IMap;
using envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig;

// TODO: Consider putting responses into cache with TTL derived from `age` header.
//  instead of using a common TTL for all.

// TODO: Mention about max.lease.time option on readme doc.

class HazelcastHttpCache : public HttpCache,
                           public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastHttpCache(HazelcastHttpCacheConfig config);

  // Cache::HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(LookupContextPtr&& lookup_context,
      Http::ResponseHeaderMapPtr&& response_headers) override;
  CacheInfo cacheInfo() const override;

  // Divided profile
  void putHeader(const uint64_t& key, const HazelcastHeaderEntry& entry);
  void putBody(const uint64_t& key, const uint64_t& order, const HazelcastBodyEntry& entry);
  HazelcastHeaderPtr getHeader(const uint64_t& key);
  HazelcastBodyPtr getBody(const uint64_t& key, const uint64_t& order);

  // Unified profile
  void putResponseIfAbsent(const uint64_t& key, const HazelcastResponseEntry& entry);
  HazelcastResponsePtr getResponse(const uint64_t& key);

  // Hazelcast cluster connection
  void connect();
  void shutdown();

  // Recoveries for malformed entries
  void onMissingBody(uint64_t key, int32_t version, uint64_t body_size);
  void onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size);

  // Internal lock mechanism of Hazelcast specific to map and key pair is
  // used to make exactly one lookup context responsible for insertions and
  // secure consistency during updateHeaders(). These locks prevent possible
  // race for multiple cache filters from multiple proxies when they connect
  // to the same Hazelcast cluster.
  bool tryLock(const uint64_t& key);
  void unlock(const uint64_t& key);

  const uint64_t& bodySizePerEntry() { return body_partition_size_; };
  const uint64_t& maxBodySize() { return max_body_size_; };

  inline int64_t mapKey(const uint64_t& unsigned_key) {
    // Hazelcast client accepts signed keys on maps. The reason for not static casting
    // directly is a possible overflow for int64 on intermediate step for -2^63.
    int64_t signed_key;
    std::memcpy (&signed_key, &unsigned_key, sizeof(int64_t));
    return signed_key;
  }

  /**
   * Creates string keys for body partition entries.
   *
   * @param key     Unsigned hash key for the header.
   * @param order   Order of the body among other partitions starting from 0.
   * @return        Body partition key for header and order pair.
   *
   * @note          Appending '#' or any other marker between the key and order
   *                string is required. Otherwise, for instance, the 11th order
   *                body for key 1 and the 1st order body for key 11 will have
   *                the same map key "111".
   */
  inline std::string orderedMapKey(const uint64_t& key, const uint64_t& order) {
    return std::to_string(key).append("#").append(std::to_string(order));
  }

  uint64_t random() {
    return rand_.random();
  }

  ~HazelcastHttpCache() {
    shutdown();
  };

private:
  friend class HazelcastHttpCacheTestBase;

  void updateUnifiedHeaders(LookupContextPtr&& lookup_context,
      Http::ResponseHeaderMapPtr&& response_headers);
  void updateDividedHeaders(LookupContextPtr&& lookup_context,
      Http::ResponseHeaderMapPtr&& response_headers);

  // Maps are differentiated by their names in Hazelcast cluster. Hence each
  // plugin will connect to a map named with partition size and app_prefix.
  // When a cache connects to a cluster which already has an active cache
  // with different body_partition_size, this naming will prevent incompatibility
  // and separate these two caches in the Hazelcast cluster.
  std::string constructMapName(const std::string& postfix);

  inline IMap<int64_t, HazelcastHeaderEntry> getHeaderMap() {
    return hazelcast_client_->getMap<int64_t, HazelcastHeaderEntry>(header_map_name_);
  }

  inline IMap<std::string, HazelcastBodyEntry> getBodyMap() {
    return hazelcast_client_->getMap<std::string, HazelcastBodyEntry>(body_map_name_);
  }

  inline IMap<int64_t, HazelcastResponseEntry> getResponseMap() {
    return hazelcast_client_->getMap<int64_t, HazelcastResponseEntry>(response_map_name_);
  }

  std::unique_ptr<HazelcastClient> hazelcast_client_;
  HazelcastHttpCacheConfig cache_config_;

  // From cache configuration
  const bool unified_;
  const uint64_t body_partition_size_;
  const uint64_t max_body_size_;

  std::string body_map_name_;
  std::string header_map_name_;
  std::string response_map_name_;

  Runtime::RandomGeneratorImpl rand_;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
