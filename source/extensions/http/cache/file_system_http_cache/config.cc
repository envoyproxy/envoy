#include <memory>
#include <string>

#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.h"
#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/common/async_files/async_file_manager_factory.h"
#include "source/extensions/filters/http/cache/active_cache.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

/**
 * Returns a copy of the original ConfigProto with a slash appended to cache_path
 * if one was not present.
 * @param original the original ConfigProto.
 * @return the normalized ConfigProto.
 */
ConfigProto normalizeConfig(const ConfigProto& original) {
  ConfigProto config = original;
  if (!absl::EndsWith(config.cache_path(), "/") && !absl::EndsWith(config.cache_path(), "\\")) {
    config.set_cache_path(absl::StrCat(config.cache_path(), "/"));
  }
  return config;
}

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
  CacheSingleton(
      std::shared_ptr<Common::AsyncFiles::AsyncFileManagerFactory>&& async_file_manager_factory,
      Thread::ThreadFactory& thread_factory)
      : async_file_manager_factory_(async_file_manager_factory),
        cache_eviction_thread_(thread_factory) {}

  std::shared_ptr<ActiveCache> get(std::shared_ptr<CacheSingleton> singleton,
                                   const ConfigProto& non_normalized_config,
                                   Server::Configuration::ServerFactoryContext& context) {
    std::shared_ptr<ActiveCache> cache;
    ConfigProto config = normalizeConfig(non_normalized_config);
    auto key = config.cache_path();
    absl::MutexLock lock(&mu_);
    auto it = caches_.find(key);
    if (it != caches_.end()) {
      cache = it->second.lock();
    }
    if (!cache) {
      std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager =
          async_file_manager_factory_->getAsyncFileManager(config.manager_config());
      std::unique_ptr<FileSystemHttpCache> fs_cache = std::make_unique<FileSystemHttpCache>(
          singleton, cache_eviction_thread_, std::move(config), std::move(async_file_manager),
          context.scope());
      cache = ActiveCache::create(context.timeSource(), std::move(fs_cache));
      caches_[key] = cache;
    } else {
      // Check that the config of the cache found in the lookup table for the given path
      // has the same config as the config being added.
      FileSystemHttpCache& fs_cache = static_cast<FileSystemHttpCache&>(cache->cache());
      if (!Protobuf::util::MessageDifferencer::Equals(fs_cache.config(), config)) {
        throw EnvoyException(
            fmt::format("mismatched FileSystemHttpCacheConfig with same path\n{}\nvs.\n{}",
                        fs_cache.config().DebugString(), config.DebugString()));
      }
    }
    return cache;
  }

private:
  std::shared_ptr<Common::AsyncFiles::AsyncFileManagerFactory> async_file_manager_factory_;
  CacheEvictionThread cache_eviction_thread_;
  absl::Mutex mu_;
  // We keep weak_ptr here so the caches can be destroyed if the config is updated to stop using
  // that config of cache. The caches each keep shared_ptrs to this singleton, which keeps the
  // singleton from being destroyed unless it's no longer keeping track of any caches.
  // (The singleton shared_ptr is *only* held by cache instances.)
  absl::flat_hash_map<std::string, std::weak_ptr<ActiveCache>> caches_ ABSL_GUARDED_BY(mu_);
};

SINGLETON_MANAGER_REGISTRATION(file_system_http_cache_singleton);

class FileSystemHttpCacheFactory : public HttpCacheFactory {
public:
  // From UntypedFactory
  std::string name() const override { return std::string{FileSystemHttpCache::name()}; }
  // From TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
  // From HttpCacheFactory
  std::shared_ptr<ActiveCache>
  getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& filter_config,
           Server::Configuration::ServerFactoryContext& context) override {
    ConfigProto config;
    THROW_IF_NOT_OK(MessageUtil::unpackTo(filter_config.typed_config(), config));
    std::shared_ptr<CacheSingleton> caches = context.singletonManager().getTyped<CacheSingleton>(
        SINGLETON_MANAGER_REGISTERED_NAME(file_system_http_cache_singleton), [&context] {
          return std::make_shared<CacheSingleton>(
              Common::AsyncFiles::AsyncFileManagerFactory::singleton(&context.singletonManager()),
              context.api().threadFactory());
        });
    return caches->get(caches, config, context);
  }
};

static Registry::RegisterFactory<FileSystemHttpCacheFactory, HttpCacheFactory> register_;

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
