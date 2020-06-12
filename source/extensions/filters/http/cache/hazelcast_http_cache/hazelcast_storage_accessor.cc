#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_storage_accessor.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

void HazelcastClusterAccessor::putHeader(const int64_t map_key, const HazelcastHeaderEntry& value) {
  getHeaderMap().set(map_key, value);
}

void HazelcastClusterAccessor::putBody(const std::string& map_key,
                                       const HazelcastBodyEntry& value) {
  getBodyMap().set(map_key, value);
}

HazelcastHeaderPtr HazelcastClusterAccessor::getHeader(const int64_t map_key) {
  return getHeaderMap().get(map_key);
}

HazelcastBodyPtr HazelcastClusterAccessor::getBody(const std::string& map_key) {
  return getBodyMap().get(map_key);
}

void HazelcastClusterAccessor::removeBodyAsync(const std::string& map_key) {
  getBodyMap().removeAsync(map_key);
}

void HazelcastClusterAccessor::removeHeader(const int64_t map_key) {
  getHeaderMap().deleteEntry(map_key);
}

void HazelcastClusterAccessor::putResponse(const int64_t map_key,
                                           const HazelcastResponseEntry& value) {
  getResponseMap().set(map_key, value);
}

HazelcastResponsePtr HazelcastClusterAccessor::getResponse(const int64_t map_key) {
  return getResponseMap().get(map_key);
}

// Internal lock mechanism of Hazelcast specific to map and key pair is
// used to make exactly one lookup context responsible for insertions. It
// is and also used to secure consistency during updateHeaders(). These
// locks prevent possible race for multiple cache filters from multiple
// proxies when they connect to the same Hazelcast cluster. The locks used
// here are re-entrant. A locked key can be acquired by the same thread
// again and again based on its pid.
bool HazelcastClusterAccessor::tryLock(const int64_t map_key, bool unified) {
  return unified ? getResponseMap().tryLock(map_key) : getHeaderMap().tryLock(map_key);
}

// Hazelcast does not allow a thread to unlock a key unless it's the key
// owner. To handle this, IMap::forceUnlock is called here to make sure
// the lock is released certainly.
void HazelcastClusterAccessor::unlock(const int64_t map_key, bool unified) {
  if (unified) {
    getResponseMap().forceUnlock(map_key);
  } else {
    getHeaderMap().forceUnlock(map_key);
  }
}

bool HazelcastClusterAccessor::isRunning() const {
  return hazelcast_client_ ? hazelcast_client_->getLifecycleService().isRunning() : false;
}

std::string HazelcastClusterAccessor::clusterName() const {
  return hazelcast_client_ ? hazelcast_client_->getClientConfig().getGroupConfig().getName() : "";
}

void HazelcastClusterAccessor::disconnect() {
  if (hazelcast_client_) {
    hazelcast_client_->shutdown();
  }
}

HazelcastClusterAccessor::HazelcastClusterAccessor(HazelcastHttpCache& cache,
                                                   ClientConfig&& client_config,
                                                   const std::string& app_prefix,
                                                   const uint64_t partition_size)
    : cache_(cache), app_prefix_(app_prefix), partition_size_(partition_size),
      client_config_(std::move(client_config)) {
  body_map_name_ = constructMapName("body", false);
  header_map_name_ = constructMapName("div", false);
  response_map_name_ = constructMapName("uni", true);
}

void HazelcastClusterAccessor::connect() {
  if (hazelcast_client_ && hazelcast_client_->getLifecycleService().isRunning()) {
    return;
  }
  hazelcast_client_ = std::make_unique<HazelcastClient>(client_config_);
  listener_ = std::make_unique<HeaderMapEntryListener>(cache_);
  getHeaderMap().addEntryListener(*listener_, true);
}

std::string HazelcastClusterAccessor::startInfo() const {
  return absl::StrFormat("HazelcastHttpCache is created with profile: %s. Max body size: %d.\n"
                         "Cache statistics can be observed on Hazelcast Management Center "
                         "from the map named %s.",
                         cache_.unified() ?
                         "UNIFIED" :
                         "DIVIDED, partition size: " + std::to_string(partition_size_),
                         cache_.maxBodyBytes(),
                         cache_.unified() ? response_map_name_ : header_map_name_);
}

std::string HazelcastClusterAccessor::constructMapName(const std::string& postfix, bool unified) {
  return absl::StrFormat("%s-%d-%s", app_prefix_, unified ? cache_.maxBodyBytes() : partition_size_, postfix);
}

void HeaderMapEntryListener::entryEvicted(const EntryEvent<int64_t, HazelcastHeaderEntry>& event) {
  auto header = event.getOldValueObject();
  int64_t map_key = *event.getKeyObject();
  uint64_t unsigned_key;
  std::memcpy(&unsigned_key, &map_key, sizeof(uint64_t));
  // Clean up all bodies of this header via onMissingBody procedure.
  cache_.onMissingBody(unsigned_key, header->version(), header->bodySize());
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
