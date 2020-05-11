#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache_impl.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

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
  //  Depending on the filter's implementation, the cached entry's
  //  variant_key_ must be updated as well. Also, if vary headers
  //  change then the hash key of the response will change and
  //  updating only header map will not be enough in this case.
  ASSERT(lookup_context);
  ASSERT(response_headers);
  try {
    if (unified_) {
      updateUnifiedHeaders(std::move(lookup_context), std::move(response_headers));
    } else {
      updateDividedHeaders(std::move(lookup_context), std::move(response_headers));
    }
  } catch (HazelcastClientOfflineException& e) {
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
  } catch (OperationTimeoutException& e) {
    ENVOY_LOG(warn, "Updating headers has timed out.");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Updating headers has failed: {}", e.what());
  }
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

void HazelcastHttpCache::putHeader(const uint64_t key, const HazelcastHeaderEntry& entry) {
  getHeaderMap().set(mapKey(key), entry);
}

void HazelcastHttpCache::putBody(const uint64_t key, const uint64_t order,
                                 const HazelcastBodyEntry& entry) {
  getBodyMap().set(orderedMapKey(key, order), entry);
}

HazelcastHeaderPtr HazelcastHttpCache::getHeader(const uint64_t key) {
  return getHeaderMap().get(mapKey(key));
}

HazelcastBodyPtr HazelcastHttpCache::getBody(const uint64_t key, const uint64_t order) {
  return getBodyMap().get(orderedMapKey(key, order));
}

void HazelcastHttpCache::onMissingBody(uint64_t key, int32_t version, uint64_t body_size) {
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
  } catch (HazelcastClientOfflineException& e) {
    // see DividedInsertContext#insertHeader() for left over locks on a connection failure.
    ENVOY_LOG(warn, "Hazelcast Connection is offline!");
  } catch (OperationTimeoutException& e) {
    ENVOY_LOG(warn, "Clean up for missing body has timed out.");
  } catch (std::exception& e) {
    ENVOY_LOG(warn, "Clean up for missing body has failed: {}", e.what());
  }
}

void HazelcastHttpCache::onVersionMismatch(uint64_t key, int32_t version, uint64_t body_size) {
  onMissingBody(key, version, body_size);
}

void HazelcastHttpCache::putResponseIfAbsent(const uint64_t key,
                                             const HazelcastResponseEntry& entry) {
  getResponseMap().putIfAbsent(mapKey(key), entry);
}

HazelcastResponsePtr HazelcastHttpCache::getResponse(const uint64_t key) {
  return getResponseMap().get(mapKey(key));
}

bool HazelcastHttpCache::tryLock(const uint64_t key) {
  // Internal lock mechanism of Hazelcast specific to map and key pair is
  // used to make exactly one lookup context responsible for insertions and
  // secure consistency during updateHeaders(). These locks prevent possible
  // race for multiple cache filters from multiple proxies when they connect
  // to the same Hazelcast cluster.
  // The locks used here are re-entrant. A locked key can be acquired by
  // the same thread again and again based on its pid.
  return unified_ ? getResponseMap().tryLock(mapKey(key)) : getHeaderMap().tryLock(mapKey(key));
}

void HazelcastHttpCache::unlock(const uint64_t key) {
  // Hazelcast does not allow a thread to unlock a key unless it's the key
  // owner. To handle this, forceUnlock is called.
  if (unified_) {
    getResponseMap().forceUnlock(mapKey(key));
  } else {
    getHeaderMap().forceUnlock(mapKey(key));
  }
}

uint64_t HazelcastHttpCache::random() { return rand_.random(); }

void HazelcastHttpCache::connect() {
  if (hazelcast_client_ && hazelcast_client_->getLifecycleService().isRunning()) {
    ENVOY_LOG(warn, "Client is already connected. Cluster name: {}",
              hazelcast_client_->getClientConfig().getGroupConfig().getName());
    return;
  }

  ClientConfig config = ConfigUtil::getClientConfig(cache_config_);
  config.getSerializationConfig().addDataSerializableFactory(
      HazelcastCacheEntrySerializableFactory::FACTORY_ID,
      boost::shared_ptr<serialization::DataSerializableFactory>(
          new HazelcastCacheEntrySerializableFactory()));

  try {
    hazelcast_client_ = std::make_unique<HazelcastClient>(config);
  } catch (...) {
    throw EnvoyException("Hazelcast Client could not connect to any cluster.");
  }

  ENVOY_LOG(info, "HazelcastHttpCache has been started with profile: {}. Max body size: {}.",
            unified_ ? "UNIFIED"
                     : "DIVIDED, partition size: " + std::to_string(body_partition_size_),
            max_body_size_);
  ENVOY_LOG(info,
            "Cache statistics can be observed on Hazelcast Management Center"
            " from the map named {}.",
            unified_ ? response_map_name_ : header_map_name_);
}

void HazelcastHttpCache::shutdown(bool destroy) {
  if (!hazelcast_client_) {
    ENVOY_LOG(warn, "Client is already offline.");
    return;
  }
  if (hazelcast_client_->getLifecycleService().isRunning()) {
    ENVOY_LOG(info, "Shutting down Hazelcast connection...");
    hazelcast_client_->shutdown();
    ENVOY_LOG(info, "Cache is offline now.");
  } else {
    ENVOY_LOG(warn, "Cache is already offline.");
  }
  if (destroy) {
    hazelcast_client_.reset();
  }
}

HazelcastHttpCache::~HazelcastHttpCache() { shutdown(true); }

void HazelcastHttpCache::updateUnifiedHeaders(LookupContextPtr&& lookup_context,
                                              Http::ResponseHeaderMapPtr&& response_headers) {
  // TODO(enozcan): Implement when ready on the filter side.
  ASSERT(!lookup_context);
  ASSERT(!response_headers);
}

void HazelcastHttpCache::updateDividedHeaders(LookupContextPtr&& lookup_context,
                                              Http::ResponseHeaderMapPtr&& response_headers) {
  // TODO(enozcan): Implement when ready on the filter side.
  ASSERT(!lookup_context);
  ASSERT(!response_headers);
}

std::string HazelcastHttpCache::constructMapName(const std::string& postfix) {
  // Maps are differentiated by their names in Hazelcast cluster. Hence each
  // plugin will connect to a map named with partition size and app_prefix.
  // When a cache connects to a cluster which already has an active cache
  // with different body_partition_size, this naming will prevent incompatibility
  // and separate these two caches in the Hazelcast cluster.
  std::string name(cache_config_.app_prefix());
  if (!unified_) {
    name.append(":").append(std::to_string(body_partition_size_));
  }
  return name.append("-").append(postfix);
}

HazelcastHttpCache::HazelcastHttpCache(HazelcastHttpCacheConfig config)
    : HazelcastCache(config.unified(), ConfigUtil::validPartitionSize(config.body_partition_size()),
                     ConfigUtil::validMaxBodySize(config.max_body_size(), config.unified())),
      cache_config_(config) {
  body_map_name_ = constructMapName("body");
  header_map_name_ = constructMapName("div");
  response_map_name_ = constructMapName("uni");
}

std::string HazelcastHttpCacheFactory::name() const { return std::string(HazelcastCacheName); }

ProtobufTypes::MessagePtr HazelcastHttpCacheFactory::createEmptyConfigProto() {
  return std::make_unique<HazelcastHttpCacheConfig>();
}

HttpCache& HazelcastHttpCacheFactory::getCache(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) {
  if (!cache_) {
    HazelcastHttpCacheConfig hz_cache_config;
    MessageUtil::unpackTo(config.typed_config(), hz_cache_config);
    cache_ = std::make_unique<HazelcastHttpCache>(hz_cache_config);
    cache_->connect();
  }
  return *cache_;
}

HttpCache& HazelcastHttpCacheFactory::getOfflineCache(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config) {
  if (!cache_) {
    HazelcastHttpCacheConfig hz_cache_config;
    MessageUtil::unpackTo(config.typed_config(), hz_cache_config);
    cache_ = std::make_unique<HazelcastHttpCache>(hz_cache_config);
  }
  cache_->shutdown(false);
  return *cache_;
}

static Registry::RegisterFactory<HazelcastHttpCacheFactory, HttpCacheFactory> register_;

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
