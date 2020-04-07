#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Test interface for cache.
 */
class HazelcastTestableHttpCache {
public:
  HazelcastTestableHttpCache(HazelcastCache& cache) : cache_(cache){};

  virtual void clearTestMaps() PURE;
  virtual void dropTestConnection() PURE;
  virtual void restoreTestConnection() PURE;

  virtual int headerTestMapSize() PURE;
  virtual int bodyTestMapSize() PURE;
  virtual int responseTestMapSize() PURE;

  virtual void putTestResponse(uint64_t key, const HazelcastResponseEntry& entry) PURE;
  virtual void removeTestBody(uint64_t key, uint64_t order) PURE;

  virtual ~HazelcastTestableHttpCache() = default;

  HazelcastCache& base() { return cache_; }

private:
  HazelcastCache& cache_;
};

/**
 * Testable cache with local storage.
 *
 * Does not connect to a Hazelcast cluster but instead stores entries locally and
 * mimics the remote cache behavior. Running a Hazelcast cluster or a single member
 * is not needed during test.
 */
class HazelcastTestableLocalCache : public HazelcastTestableHttpCache, public HazelcastCache {
public:
  HazelcastTestableLocalCache(HazelcastHttpCacheConfig config);

  // HazelcastTestableHttpCache
  void clearTestMaps() override;
  void dropTestConnection() override;
  void restoreTestConnection() override;
  int headerTestMapSize() override;
  int bodyTestMapSize() override;
  int responseTestMapSize() override;
  void putTestResponse(uint64_t key, const HazelcastResponseEntry& entry) override;
  void removeTestBody(uint64_t key, uint64_t order) override;

  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(LookupContextPtr&& lookup_context,
                     Http::ResponseHeaderMapPtr&& response_headers) override;
  CacheInfo cacheInfo() const override;

  // HazelcastCache
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

private:
  void checkConnection() {
    if (!connected_) {
      throw std::exception();
    }
  }

  std::unordered_map<int64_t, HazelcastHeaderPtr> headerMap;
  std::unordered_map<std::string, HazelcastBodyPtr> bodyMap;
  std::unordered_map<int64_t, HazelcastResponsePtr> responseMap;

  std::vector<uint64_t> headerLocks;
  std::vector<uint64_t> responseLocks;

  bool connected_ = false;
  uint32_t random_counter_ = 0;
};

/**
 * Test wrapper for HazelcastHttpCache.
 *
 * Establishes a TCP connection with an alive Hazelcast member during tests and
 * stores data in distributed map. Simulates the original version of the cache.
 */
class HazelcastTestableRemoteCache : public HazelcastTestableHttpCache, public HazelcastHttpCache {
public:
  HazelcastTestableRemoteCache(HazelcastHttpCacheConfig config)
      : HazelcastTestableHttpCache(dynamic_cast<HazelcastCache&>(*this)),
        HazelcastHttpCache(config) {}

  void clearTestMaps() override {
    if (unified_) {
      getResponseMap().clear();
    } else {
      getBodyMap().clear();
      getHeaderMap().clear();
    }
  }

  void dropTestConnection() override { shutdown(false); }

  void restoreTestConnection() override { connect(); }

  int headerTestMapSize() override { return getHeaderMap().size(); }

  int bodyTestMapSize() override { return getBodyMap().size(); }

  int responseTestMapSize() override { return getResponseMap().size(); }

  void removeTestBody(uint64_t key, uint64_t order) override {
    getBodyMap().remove(orderedMapKey(key, order));
  }

  void putTestResponse(uint64_t key, const HazelcastResponseEntry& entry) override {
    getResponseMap().put(mapKey(key), entry);
  }
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
