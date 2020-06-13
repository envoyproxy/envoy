#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using hazelcast::client::ClientConfig;
using hazelcast::client::exception::HazelcastClientOfflineException;
using hazelcast::client::serialization::DataSerializableFactory;

HazelcastHttpCache::HazelcastHttpCache(
    HazelcastHttpCacheConfig&& typed_config,
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& cache_config)
    : unified_(typed_config.unified()),
      body_partition_size_(ConfigUtil::validPartitionSize(typed_config.body_partition_size())),
      max_body_bytes_(
          ConfigUtil::validMaxBodySize(cache_config.max_body_bytes(), typed_config.unified())),
      cache_config_(std::move(typed_config)) {}

void HazelcastHttpCache::onMissingBody(uint64_t key_hash, int32_t version, uint64_t body_size) {
  try {
    if (!tryLock(key_hash)) {
      // If multiple onMissingBody calls are made for the same key hash simultaneously,
      // the locking here will allow only one of them to perform clean up.
      return;
    }
    auto header = getHeader(key_hash);
    if (header && header->version() != version) {
      // The missed body does not belong to the looked up header. Probably eviction and then
      // insertion for the header has happened in the meantime. Since new insertion will
      // override the existing bodies, ignore the cleanup and let orphan bodies (belong to
      // evicted header, not overridden) be evicted by TTL as well.
      unlock(key_hash);
      return;
    }
    int body_count = body_size / body_partition_size_;
    while (body_count >= 0) {
      accessor_->removeBodyAsync(orderedMapKey(key_hash, body_count--));
    }
    accessor_->removeHeader(mapKey(key_hash));
    unlock(key_hash);
  } catch (HazelcastClientOfflineException& e) {
    // see DividedInsertContext::insertHeader for left over locks on a connection failure.
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Clean up for missing body has failed: {}", e.what());
  }
}

void HazelcastHttpCache::onVersionMismatch(uint64_t key_hash, int32_t version, uint64_t body_size) {
  onMissingBody(key_hash, version, body_size);
}

void HazelcastHttpCache::start(StorageAccessorPtr&& accessor) {
  if (accessor_ && accessor_->isRunning()) {
    ENVOY_LOG(warn, "Client is already connected. Cluster name: {}", accessor_->clusterName());
    return;
  }

  if (!accessor_) {
    accessor_ = std::move(accessor);
  }

  try {
    accessor_->connect();
  } catch (...) {
    accessor_.reset();
    throw EnvoyException("Hazelcast Client could not connect to any cluster.");
  }
  ENVOY_LOG(info, accessor_->startInfo());
}

void HazelcastHttpCache::shutdown(bool destroy) {
  if (!accessor_) {
    ENVOY_LOG(warn, "Cache is already offline.");
    return;
  }
  if (accessor_->isRunning()) {
    ENVOY_LOG(info, "Shutting down Hazelcast connection...");
    accessor_->disconnect();
    ENVOY_LOG(info, "Cache is offline now.");
  } else {
    ENVOY_LOG(warn, "Hazelcast client is already disconnected.");
  }
  if (destroy) {
    accessor_.reset();
  }
}

HazelcastHttpCache::~HazelcastHttpCache() { shutdown(true); }

LookupContextPtr HazelcastHttpCache::makeLookupContext(LookupRequest&& request) {
  if (unified_) {
    return std::make_unique<UnifiedLookupContext>(*this, std::move(request));
  } else {
    return std::make_unique<DividedLookupContext>(*this, std::move(request));
  }
}

InsertContextPtr HazelcastHttpCache::makeInsertContext(LookupContextPtr&& lookup_context) {
  ASSERT(lookup_context != nullptr);
  if (unified_) {
    return std::make_unique<UnifiedInsertContext>(*lookup_context, *this);
  } else {
    return std::make_unique<DividedInsertContext>(*lookup_context, *this);
  }
}

// TODO(enozcan): Implement when it's ready on the filter side.
//  Depending on the filter's implementation, the cached entry's
//  variant_key_ must be updated as well. Also, if vary headers
//  change then the key hash of the response will change and
//  updating only header map will not be enough in this case.
void HazelcastHttpCache::updateHeaders(LookupContextPtr&&, Http::ResponseHeaderMapPtr&&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

constexpr absl::string_view HazelcastCacheName = "envoy.extensions.http.cache.hazelcast";

// Cluster wide cache statistics should be observed on Hazelcast Management Center.
// They are not stored locally.
CacheInfo HazelcastHttpCache::cacheInfo() const {
  CacheInfo cache_info;
  cache_info.name_ = HazelcastCacheName;
  cache_info.supports_range_requests_ = true;
  return cache_info;
}

std::string HazelcastHttpCacheFactory::name() const { return std::string(HazelcastCacheName); }

ProtobufTypes::MessagePtr HazelcastHttpCacheFactory::createEmptyConfigProto() {
  return std::make_unique<HazelcastHttpCacheConfig>();
}

HttpCache& HazelcastHttpCacheFactory::getCache(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) {
  if (!cache_) {
    HazelcastHttpCacheConfig typed_config;
    MessageUtil::unpackTo(config.typed_config(), typed_config);
    ClientConfig client_config = ConfigUtil::getClientConfig(typed_config);
    cache_ = std::make_unique<HazelcastHttpCache>(std::move(typed_config), config);
    StorageAccessorPtr accessor = std::make_unique<HazelcastClusterAccessor>(
        *cache_, std::move(client_config), cache_->prefix(), cache_->bodySizePerEntry());
    cache_->start(std::move(accessor));
  }
  return *cache_;
}

HazelcastHttpCachePtr HazelcastHttpCacheFactory::getOfflineCache(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) {
  if (!cache_) {
    HazelcastHttpCacheConfig hz_cache_config;
    MessageUtil::unpackTo(config.typed_config(), hz_cache_config);
    cache_ = std::make_unique<HazelcastHttpCache>(std::move(hz_cache_config), config);
  }
  return std::move(cache_);
}

static Registry::RegisterFactory<HazelcastHttpCacheFactory, HttpCacheFactory> register_;

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
