#pragma once

#include "envoy/registry/registry.h"

#include "common/common/logger.h"
#include "common/runtime/runtime_impl.h"

#include "source/extensions/filters/http/cache/hazelcast_http_cache/config.pb.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache.h"

#include "hazelcast/client/HazelcastClient.h"
#include "hazelcast/client/IMap.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig;
using hazelcast::client::IMap;

// TODO(enozcan): Consider putting responses into cache with TTL derived from `max-age` header
//  instead of using a common TTL for all. This is possible during insertion by passing TTL
//  amount regardless of the configured TTL on Hazelcast server side.
//  i.e: IMap::put(const K &key, const V &value, int64_t ttlInMilliseconds);

class HazelcastHttpCache : public HazelcastCache,
                           public Logger::Loggable<Logger::Id::hazelcast_http_cache> {
public:
  HazelcastHttpCache(HazelcastHttpCacheConfig config);

  // from Cache::HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(LookupContextPtr&& lookup_context,
                     Http::ResponseHeaderMapPtr&& response_headers) override;
  CacheInfo cacheInfo() const override;

  // from HazelcastCache
  void putHeader(const uint64_t key, const HazelcastHeaderEntry& entry) override;
  void putBody(const uint64_t key, const uint64_t order, const HazelcastBodyEntry& entry) override;
  HazelcastHeaderPtr getHeader(const uint64_t key) override;
  HazelcastBodyPtr getBody(const uint64_t key, const uint64_t order) override;
  void onMissingBody(uint64_t key, int32_t version, uint64_t body_size) override;
  void onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size) override;
  void putResponseIfAbsent(const uint64_t key, const HazelcastResponseEntry& entry) override;
  HazelcastResponsePtr getResponse(const uint64_t key) override;
  bool tryLock(const uint64_t key) override;
  void unlock(const uint64_t key) override;
  uint64_t random() override;

  void connect();
  void shutdown(bool destroy);

  ~HazelcastHttpCache();

private:
  friend class HazelcastHttpCacheTestBase;
  friend class HazelcastTestableRemoteCache;

  void updateUnifiedHeaders(LookupContextPtr&& lookup_context,
                            Http::ResponseHeaderMapPtr&& response_headers);

  void updateDividedHeaders(LookupContextPtr&& lookup_context,
                            Http::ResponseHeaderMapPtr&& response_headers);

  /** Generates a unique map name to this cache with the given postfix. */
  std::string constructMapName(const std::string& postfix);

  /** Returns remote header cache proxy */
  inline IMap<int64_t, HazelcastHeaderEntry> getHeaderMap() {
    return hazelcast_client_->getMap<int64_t, HazelcastHeaderEntry>(header_map_name_);
  }

  /** Returns remote body cache proxy */
  inline IMap<std::string, HazelcastBodyEntry> getBodyMap() {
    return hazelcast_client_->getMap<std::string, HazelcastBodyEntry>(body_map_name_);
  }

  /** Returns remote response cache proxy */
  inline IMap<int64_t, HazelcastResponseEntry> getResponseMap() {
    return hazelcast_client_->getMap<int64_t, HazelcastResponseEntry>(response_map_name_);
  }

  std::unique_ptr<HazelcastClient> hazelcast_client_;
  HazelcastHttpCacheConfig cache_config_;

  std::string body_map_name_;
  std::string header_map_name_;
  std::string response_map_name_;

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
