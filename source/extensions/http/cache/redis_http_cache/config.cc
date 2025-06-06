#include <memory>
#include <string>

//#include "envoy/registry/registry.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {
namespace {

/**
 * A singleton that acts as a factory for generating and looking up FileSystemHttpCaches.
 * When given equivalent configs, the singleton returns pointers to the same cache.
 * When given different configs, the singleton returns different cache instances.
 * If given configs with the same cache_path but different configuration,
 * an exception is thrown, as it doesn't make sense two operate two caches in the
 * same path with different configurations.
 */
class CacheSingleton : public Envoy::Singleton::Instance {
public:
  CacheSingleton(Upstream::ClusterManager& cluster_manager,
                 ThreadLocal::SlotAllocator& slot_allocator)
      : cluster_manager_(cluster_manager), slot_allocator_(slot_allocator) {}

  std::shared_ptr<RedisHttpCache> getCache(std::shared_ptr<CacheSingleton> /*singleton*/,
                                           const ConfigProto& config,
                                           Stats::Scope& /*stats_scope*/) {
    absl::MutexLock lock(&mu_);

    auto cache = caches_.find(config.cluster());
    if (cache != caches_.end()) {
      return cache->second.lock();
    }

    // TLS internally maps into 1+ clusters.
    std::shared_ptr<RedisHttpCache> new_cache =
        std::make_shared<RedisHttpCache>(config.cluster(), cluster_manager_, slot_allocator_);

    caches_.emplace(config.cluster(), new_cache);
    return new_cache;
  }

private:
  // Each cache is identified by a cluster name.
  absl::flat_hash_map<std::string, std::weak_ptr<RedisHttpCache>> caches_ ABSL_GUARDED_BY(mu_);
  absl::Mutex mu_;
  Upstream::ClusterManager& cluster_manager_;
  ThreadLocal::SlotAllocator& slot_allocator_;
};

SINGLETON_MANAGER_REGISTRATION(redis_http_cache_singleton);

class RedisHttpCacheFactory : public HttpCacheFactory {
public:
  std::string name() const override { return std::string{RedisHttpCache::name()}; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
  // From HttpCacheFactory
  std::shared_ptr<HttpCache>
  getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& filter_config,
           Server::Configuration::FactoryContext& context) override {
    ConfigProto config;
    THROW_IF_NOT_OK(MessageUtil::unpackTo(filter_config.typed_config(), config));
    std::shared_ptr<CacheSingleton> caches =
        context.serverFactoryContext().singletonManager().getTyped<CacheSingleton>(
            SINGLETON_MANAGER_REGISTERED_NAME(redis_http_cache_singleton), [&context] {
              return std::make_shared<CacheSingleton>(
                  context.serverFactoryContext().clusterManager(),
                  context.serverFactoryContext().threadLocal());
            });
    return caches->getCache(caches, config, context.scope());
  }
};

static Registry::RegisterFactory<RedisHttpCacheFactory, HttpCacheFactory> register_;

} // namespace
} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
