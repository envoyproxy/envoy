#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_storage_accessor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

void HazelcastClusterAccessor::putHeader(const int64_t key, const HazelcastHeaderEntry& value) {
  getHeaderMap().set(key, value);
}

void HazelcastClusterAccessor::putBody(const std::string& key, const HazelcastBodyEntry& value) {
  getBodyMap().set(key, value);
}

HazelcastHeaderPtr HazelcastClusterAccessor::getHeader(const int64_t key) {
  return getHeaderMap().get(key);
}

HazelcastBodyPtr HazelcastClusterAccessor::getBody(const std::string& key) {
  return getBodyMap().get(key);
}

void HazelcastClusterAccessor::removeBodyAsync(const std::string& key) {
  getBodyMap().removeAsync(key);
}

void HazelcastClusterAccessor::removeHeader(const int64_t key) { getHeaderMap().deleteEntry(key); }

void HazelcastClusterAccessor::putResponseIfAbsent(const int64_t key,
                                                   const HazelcastResponseEntry& value) {
  getResponseMap().putIfAbsent(key, value);
}

HazelcastResponsePtr HazelcastClusterAccessor::getResponse(const int64_t key) {
  return getResponseMap().get(key);
}

// Internal lock mechanism of Hazelcast specific to map and key pair is
// used to make exactly one lookup context responsible for insertions. It
// is and also used to secure consistency during updateHeaders(). These
// locks prevent possible race for multiple cache filters from multiple
// proxies when they connect to the same Hazelcast cluster. The locks used
// here are re-entrant. A locked key can be acquired by the same thread
// again and again based on its pid.
bool HazelcastClusterAccessor::tryLock(const int64_t key, bool unified) {
  return unified ? getResponseMap().tryLock(key) : getHeaderMap().tryLock(key);
}

// Hazelcast does not allow a thread to unlock a key unless it's the key
// owner. To handle this, IMap::forceUnlock is called here to make sure
// the lock is released certainly.
void HazelcastClusterAccessor::unlock(const int64_t key, bool unified) {
  if (unified) {
    getResponseMap().forceUnlock(key);
  } else {
    getHeaderMap().forceUnlock(key);
  }
}

bool HazelcastClusterAccessor::isRunning() {
  if (hazelcast_client_) {
    return hazelcast_client_->getLifecycleService().isRunning();
  }
  return false;
}

std::string HazelcastClusterAccessor::clusterName() {
  return hazelcast_client_->getClientConfig().getGroupConfig().getName();
}

void HazelcastClusterAccessor::disconnect() { hazelcast_client_->shutdown(); }

const std::string& HazelcastClusterAccessor::headerMapName() { return header_map_name_; }

const std::string& HazelcastClusterAccessor::responseMapName() { return response_map_name_; }

HazelcastClusterAccessor::HazelcastClusterAccessor(ClientConfig&& client_config,
                                                   const std::string& app_prefix,
                                                   const uint64_t partition_size)
    : app_prefix_(app_prefix), partition_size_(partition_size),
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
}

std::string HazelcastClusterAccessor::constructMapName(const std::string& postfix, bool unified) {
  std::string name(app_prefix_);
  if (!unified) {
    name.append(":").append(std::to_string(partition_size_));
  }
  return name.append("-").append(postfix);
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
