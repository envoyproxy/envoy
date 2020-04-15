#include "test/extensions/filters/http/cache/hazelcast_http_cache/hazelcast_test_cache.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

HazelcastTestableLocalCache::HazelcastTestableLocalCache(HazelcastHttpCacheConfig config)
    : HazelcastTestableHttpCache(dynamic_cast<HazelcastCache&>(*this)),
      HazelcastCache(config.unified(), config.body_partition_size(), config.max_body_size()) {}

void HazelcastTestableLocalCache::clearTestMaps() {
  header_map_.clear();
  body_map_.clear();
  response_map_.clear();
}

void HazelcastTestableLocalCache::dropTestConnection() { connected_ = false; }

void HazelcastTestableLocalCache::restoreTestConnection() { connected_ = true; }

int HazelcastTestableLocalCache::headerTestMapSize() {
  ASSERT(!unified_);
  return header_map_.size();
}

int HazelcastTestableLocalCache::bodyTestMapSize() {
  ASSERT(!unified_);
  return body_map_.size();
}

int HazelcastTestableLocalCache::responseTestMapSize() {
  ASSERT(unified_);
  return response_map_.size();
}

void HazelcastTestableLocalCache::putTestResponse(uint64_t key,
                                                  const HazelcastResponseEntry& entry) {
  checkConnection();
  response_map_[mapKey(key)] = HazelcastResponsePtr(new HazelcastResponseEntry(entry));
}

void HazelcastTestableLocalCache::removeTestBody(uint64_t key, uint64_t order) {
  checkConnection();
  body_map_.erase(orderedMapKey(key, order));
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

CacheInfo HazelcastTestableLocalCache::cacheInfo() const { return {}; }

void HazelcastTestableLocalCache::putHeader(const uint64_t key, const HazelcastHeaderEntry& entry) {
  checkConnection();
  header_map_[mapKey(key)] = HazelcastHeaderPtr(new HazelcastHeaderEntry(entry));
}

void HazelcastTestableLocalCache::putBody(const uint64_t key, const uint64_t order,
                                          const HazelcastBodyEntry& entry) {
  checkConnection();
  body_map_[orderedMapKey(key, order)] = HazelcastBodyPtr(new HazelcastBodyEntry(entry));
}

HazelcastHeaderPtr HazelcastTestableLocalCache::getHeader(const uint64_t key) {
  checkConnection();
  auto result = header_map_.find(mapKey(key));
  if (result != header_map_.end()) {
    // New objects are created during deserialization. Hence not returning the original one here.
    return HazelcastHeaderPtr(new HazelcastHeaderEntry(*result->second));
  } else {
    return nullptr;
  }
}

HazelcastBodyPtr HazelcastTestableLocalCache::getBody(const uint64_t key, const uint64_t order) {
  checkConnection();
  auto result = body_map_.find(orderedMapKey(key, order));
  if (result != body_map_.end()) {
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
    body_map_.erase(orderedMapKey(key, body_count--));
  }
  header_map_.erase(mapKey(key));
  unlock(key);
}
void HazelcastTestableLocalCache::onVersionMismatch(uint64_t key, int32_t version,
                                                    uint64_t body_size) {
  onMissingBody(key, version, body_size);
}

void HazelcastTestableLocalCache::putResponseIfAbsent(const uint64_t key,
                                                      const HazelcastResponseEntry& entry) {
  checkConnection();
  if (response_map_.find(mapKey(key)) != response_map_.end()) {
    return;
  }
  response_map_[mapKey(key)] = HazelcastResponsePtr(new HazelcastResponseEntry(entry));
}

HazelcastResponsePtr HazelcastTestableLocalCache::getResponse(const uint64_t key) {
  checkConnection();
  auto result = response_map_.find(mapKey(key));
  if (result != response_map_.end()) {
    return HazelcastResponsePtr(new HazelcastResponseEntry(*result->second));
  } else {
    return nullptr;
  }
}

bool HazelcastTestableLocalCache::tryLock(const uint64_t key) {
  checkConnection();
  if (unified_) {
    bool locked =
        std::find(response_locks_.begin(), response_locks_.end(), key) != response_locks_.end();
    if (locked) {
      return false;
    } else {
      response_locks_.push_back(key);
      return true;
    }
  } else {
    bool locked = std::find(header_locks_.begin(), header_locks_.end(), key) != header_locks_.end();
    if (locked) {
      return false;
    } else {
      header_locks_.push_back(key);
      return true;
    }
  }
}

void HazelcastTestableLocalCache::unlock(const uint64_t key) {
  checkConnection();
  if (unified_) {
    response_locks_.erase(std::remove(response_locks_.begin(), response_locks_.end(), key),
                          response_locks_.end());
  } else {
    header_locks_.erase(std::remove(header_locks_.begin(), header_locks_.end(), key),
                        header_locks_.end());
  }
}

uint64_t HazelcastTestableLocalCache::random() { return ++random_counter_; }

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
