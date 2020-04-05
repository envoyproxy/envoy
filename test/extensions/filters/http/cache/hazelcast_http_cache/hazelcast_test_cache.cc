#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/hazelcast_test_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

HazelcastTestableLocalCache::HazelcastTestableLocalCache(HazelcastHttpCacheConfig config)
    : HazelcastTestableHttpCache(dynamic_cast<HazelcastCache&>(*this)),
      HazelcastCache(config.unified(), config.body_partition_size(), config.max_body_size()) {}

void HazelcastTestableLocalCache::clearTestMaps() {
  headerMap.clear();
  bodyMap.clear();
  responseMap.clear();
}

void HazelcastTestableLocalCache::dropTestConnection() { connected_ = false; }

void HazelcastTestableLocalCache::restoreTestConnection() { connected_ = true; }

int HazelcastTestableLocalCache::headerTestMapSize() {
  ASSERT(!unified_);
  return headerMap.size();
}

int HazelcastTestableLocalCache::bodyTestMapSize() {
  ASSERT(!unified_);
  return bodyMap.size();
}

int HazelcastTestableLocalCache::responseTestMapSize() {
  ASSERT(unified_);
  return responseMap.size();
}

void HazelcastTestableLocalCache::putTestResponse(uint64_t key,
                                                  const HazelcastResponseEntry& entry) {
  checkConnection();
  responseMap[mapKey(key)] = HazelcastResponsePtr(new HazelcastResponseEntry(entry));
}

void HazelcastTestableLocalCache::removeTestBody(uint64_t key, uint64_t order) {
  checkConnection();
  bodyMap.erase(orderedMapKey(key, order));
}

LookupContextPtr HazelcastTestableLocalCache::makeLookupContext(LookupRequest&& request) {
  if (unified_) {
    return std::make_unique<UnifiedLookupContext>(*this, std::move(request));
  } else {
    return std::make_unique<DividedLookupContext>(*this, std::move(request));
  }
}

InsertContextPtr HazelcastTestableLocalCache::makeInsertContext(LookupContextPtr&& lookup_context) {
  ASSERT(lookup_context != nullptr);
  if (unified_) {
    return std::make_unique<UnifiedInsertContext>(*lookup_context, *this);
  } else {
    return std::make_unique<DividedInsertContext>(*lookup_context, *this);
  }
}

void HazelcastTestableLocalCache::updateHeaders(LookupContextPtr&& lookup_context,
                                                Http::ResponseHeaderMapPtr&& response_headers) {
  // Not implemented and tested yet.
  ASSERT(lookup_context);
  ASSERT(response_headers);
}

CacheInfo HazelcastTestableLocalCache::cacheInfo() const { return CacheInfo(); }

void HazelcastTestableLocalCache::putHeader(const uint64_t& key,
                                            const HazelcastHeaderEntry& entry) {
  checkConnection();
  headerMap[mapKey(key)] = HazelcastHeaderPtr(new HazelcastHeaderEntry(entry));
}

void HazelcastTestableLocalCache::putBody(const uint64_t& key, const uint64_t& order,
                                          const HazelcastBodyEntry& entry) {
  checkConnection();
  bodyMap[orderedMapKey(key, order)] = HazelcastBodyPtr(new HazelcastBodyEntry(entry));
}

HazelcastHeaderPtr HazelcastTestableLocalCache::getHeader(const uint64_t& key) {
  checkConnection();
  auto result = headerMap.find(mapKey(key));
  if (result != headerMap.end()) {
    // New objects are created during deserialization. Hence not returning the original one here.
    return HazelcastHeaderPtr(new HazelcastHeaderEntry(*result->second));
  } else {
    return nullptr;
  }
}

HazelcastBodyPtr HazelcastTestableLocalCache::getBody(const uint64_t& key, const uint64_t& order) {
  checkConnection();
  auto result = bodyMap.find(orderedMapKey(key, order));
  if (result != bodyMap.end()) {
    return HazelcastBodyPtr(new HazelcastBodyEntry(*result->second));
  } else {
    return nullptr;
  }
}

void HazelcastTestableLocalCache::onMissingBody(uint64_t key, int32_t version, uint64_t body_size) {
  if (!tryLock(key)) {
    return;
  }
  auto header = getHeader(key);
  if (header && header->version() != version) {
    unlock(key);
    return;
  }
  int body_count = body_size / body_partition_size_;
  while (body_count >= 0) {
    bodyMap.erase(orderedMapKey(key, body_count--));
  }
  headerMap.erase(mapKey(key));
  unlock(key);
}
void HazelcastTestableLocalCache::onVersionMismatch(uint64_t key, int32_t version,
                                                    uint64_t body_size) {
  onMissingBody(key, version, body_size);
}

void HazelcastTestableLocalCache::putResponseIfAbsent(const uint64_t& key,
                                                      const HazelcastResponseEntry& entry) {
  checkConnection();
  if (responseMap.find(mapKey(key)) != responseMap.end()) {
    return;
  }
  responseMap[mapKey(key)] = HazelcastResponsePtr(new HazelcastResponseEntry(entry));
}

HazelcastResponsePtr HazelcastTestableLocalCache::getResponse(const uint64_t& key) {
  checkConnection();
  auto result = responseMap.find(mapKey(key));
  if (result != responseMap.end()) {
    return HazelcastResponsePtr(new HazelcastResponseEntry(*result->second));
  } else {
    return nullptr;
  }
}

bool HazelcastTestableLocalCache::tryLock(const uint64_t& key) {
  checkConnection();
  if (unified_) {
    bool locked = std::find(responseLocks.begin(), responseLocks.end(), key) != responseLocks.end();
    if (locked) {
      return false;
    } else {
      responseLocks.push_back(key);
      return true;
    }
  } else {
    bool locked = std::find(headerLocks.begin(), headerLocks.end(), key) != headerLocks.end();
    if (locked) {
      return false;
    } else {
      headerLocks.push_back(key);
      return true;
    }
  }
}

void HazelcastTestableLocalCache::unlock(const uint64_t& key) {
  checkConnection();
  if (unified_) {
    responseLocks.erase(std::remove(responseLocks.begin(), responseLocks.end(), key),
                        responseLocks.end());
  } else {
    headerLocks.erase(std::remove(headerLocks.begin(), headerLocks.end(), key), headerLocks.end());
  }
}

uint64_t HazelcastTestableLocalCache::random() { return std::rand(); }

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
