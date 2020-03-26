#include "extensions/filters/http/cache/hazelcast_http_cache/config_util.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

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

void HazelcastHttpCache::updateHeaders(LookupContextPtr&& lookup_context,
    Http::ResponseHeaderMapPtr&& response_headers) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  // TODO(enozcan): Enable when implemented on the filter side.
  ASSERT(lookup_context);
  ASSERT(response_headers);
  if (unified_) {
    updateUnifiedHeaders(std::move(lookup_context), std::move(response_headers));
  } else {
    updateDividedHeaders(std::move(lookup_context), std::move(response_headers));
  }
}

constexpr absl::string_view HazelcastCacheName = "envoy.extensions.http.cache.hazelcast";

// Cluster wide cache statistics should be observed on Hazelcast Management Center.
// Hence they are not stored locally.
CacheInfo HazelcastHttpCache::cacheInfo() const {
  CacheInfo cache_info;
  cache_info.name_ = HazelcastCacheName;
  cache_info.supports_range_requests_ = true;
  return cache_info;
}

void HazelcastHttpCache::putHeader(const uint64_t& key, const HazelcastHeaderEntry& entry) {
  getHeaderMap().set(mapKey(key), entry);
}

void HazelcastHttpCache::putBody(const uint64_t& key, const uint64_t& order,
    const HazelcastBodyEntry& entry) {
  getBodyMap().set(orderedMapKey(key, order), entry);
}

HazelcastHeaderPtr HazelcastHttpCache::getHeader(const uint64_t& key) {
  return getHeaderMap().get(mapKey(key));
}

HazelcastBodyPtr HazelcastHttpCache::getBody(const uint64_t& key, const uint64_t& order) {
  return getBodyMap().get(orderedMapKey(key, order));
}

void HazelcastHttpCache::putResponseIfAbsent(const uint64_t& key,
    const HazelcastResponseEntry& entry) {
  getResponseMap().putIfAbsent(mapKey(key), entry);
}

HazelcastResponsePtr HazelcastHttpCache::getResponse(const uint64_t& key) {
  return getResponseMap().get(mapKey(key));
}

void HazelcastHttpCache::connect() {
  if (hazelcast_client_) {
    ENVOY_LOG(warn, "Client is already connected. Cluster name: {}",
        hazelcast_client_->getClientConfig().getGroupConfig().getName());
    return;
  }

  ClientConfig config = ConfigUtil::getClientConfig(cache_config_);
  config.getSerializationConfig().addDataSerializableFactory(
      HazelcastCacheEntrySerializableFactory::FACTORY_ID,
      boost::shared_ptr<serialization::DataSerializableFactory>
      (new HazelcastCacheEntrySerializableFactory()));

  try {
    hazelcast_client_ = std::make_unique<HazelcastClient>(config);
  } catch (...) {
    throw EnvoyException("Hazelcast Client could not connect to any cluster.");
  }

  ENVOY_LOG(info, "HazelcastHttpCache has been started with profile: {}. Max body size: {}.",
       unified_ ? "UNIFIED" : "DIVIDED, partition size: " + std::to_string(body_partition_size_),
       max_body_size_);
  ENVOY_LOG(info, "Cache statistics can be observed on Hazelcast Management Center"
                  " from the map named {}.", unified_ ? response_map_name_ : header_map_name_);
}

void HazelcastHttpCache::shutdown() {
  if (hazelcast_client_) {
    ENVOY_LOG(info, "Shutting down Hazelcast connection...");
    hazelcast_client_->shutdown();
    hazelcast_client_.release();
    ENVOY_LOG(info, "Cache is offline now.");
  }
}

void HazelcastHttpCache::onMissingBody(uint64_t key, int32_t version,  uint64_t body_size) {
  try {
    if (!tryLock(key)) {
      // Let lock owner context to recover it.
      return;
    }
    auto header = getHeader(key);
    if (header && header->version() != version) {
      // The missed body does not belong to the looked up header. Probably eviction and then
      // insertion for the header has happened in the meantime. Since new insertion will
      // override the existing bodies, ignore the cleanup and let orphan bodies (belong to
      // evicted header, not overridden) be evicted by TTL as well.
      unlock(key);
      return;
    }
    int body_count = body_size / body_partition_size_;
    while (body_count >= 0) {
      getBodyMap().removeAsync(orderedMapKey(key, body_count--));
    }
    getHeaderMap().remove(mapKey(key));
    unlock(key);
  } catch (HazelcastClientOfflineException e) {
    // see DividedInsertContext#flushHeader() for left over locks on a connection failure.
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
  }
};

void HazelcastHttpCache::onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size) {
  onMissingBody(key, version, body_size);
}

bool HazelcastHttpCache::tryLock(const uint64_t& key) {
  return unified_ ? getResponseMap().tryLock(mapKey(key)) :
         getHeaderMap().tryLock(mapKey(key));
}

void HazelcastHttpCache::unlock(const uint64_t& key) {
  // Hazelcast does not allow a thread to unlock a key unless it's the key
  // owner. To handle this, forceUnlock is called.
  if (unified_) {
    getResponseMap().forceUnlock(mapKey(key));
  } else {
    getHeaderMap().forceUnlock(mapKey(key));
  }
}

void HazelcastHttpCache::updateUnifiedHeaders(LookupContextPtr&& lookup_context,
    Http::ResponseHeaderMapPtr&& response_headers) {
  const uint64_t& key = static_cast<UnifiedLookupContext*>(lookup_context.get())->variantHashKey();
  HazelcastResponsePtr response = getResponse(key);
  if (!response) {
    // Might be evicted in meantime
    ENVOY_LOG(debug, "Updating unified headers ({}) is aborted due to lookup miss.", key);
    return;
  }
  HazelcastResponseEntry updated = *response;
  updated.header().headerMap(std::move(response_headers));
  // Update headers if no other update is performed in meantime.
  getResponseMap().replace(key, updated, *response);
};

void HazelcastHttpCache::updateDividedHeaders(LookupContextPtr&& lookup_context,
    Http::ResponseHeaderMapPtr&& response_headers) {
  const uint64_t& key = static_cast<UnifiedLookupContext*>(lookup_context.get())->variantHashKey();
  HazelcastHeaderPtr stale = getHeader(key);
  if (!stale) {
    // Might be evicted in meantime
    ENVOY_LOG(debug, "Updating divided headers ({}) is aborted due to lookup miss.", key);
    return;
  }
  HazelcastHeaderEntry updated = *stale;
  updated.headerMap(std::move(response_headers));
  // Update headers if no other update is performed in meantime.
  getHeaderMap().replace(key, updated, *stale);
};

std::string HazelcastHttpCache::constructMapName(const std::string& postfix) {
  return std::string(cache_config_.app_prefix())
    .append(":")
    .append(std::to_string(body_partition_size_))
    .append(":")
    .append(postfix);
}

HazelcastHttpCache::HazelcastHttpCache(HazelcastHttpCacheConfig config)
    : cache_config_(config), unified_(config.unified()),
    body_partition_size_(ConfigUtil::validPartitionSize(config.body_partition_size())),
    max_body_size_(ConfigUtil::validMaxBodySize(config.max_body_size())) {
  body_map_name_ = constructMapName("body");
  header_map_name_ = constructMapName("header");
  response_map_name_ = constructMapName("response");
};

class HazelcastHttpCacheFactory : public HttpCacheFactory {
public:
  // From UntypedFactory
  std::string name() const override { return std::string(HazelcastCacheName); }
  // From TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<HazelcastHttpCacheConfig>();
  }
  // From HttpCacheFactory
  HttpCache&
  getCache(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) override {
    HazelcastHttpCacheConfig hz_cache_config;
    MessageUtil::unpackTo(config.typed_config(), hz_cache_config);
    cache_ = std::make_unique<HazelcastHttpCache>(hz_cache_config);
    cache_->connect();
    return *cache_;
  }
private:
  std::unique_ptr<HazelcastHttpCache> cache_;
};

static Registry::RegisterFactory<HazelcastHttpCacheFactory, HttpCacheFactory> register_;

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
