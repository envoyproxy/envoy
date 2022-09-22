#include <memory>
#include <string>

#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.h"
#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/common/async_files/async_file_manager_factory.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

class CacheSingleton : public Envoy::Singleton::Instance {
public:
  CacheSingleton(
      TimeSource& time_source,
      std::shared_ptr<Common::AsyncFiles::AsyncFileManagerFactory>&& async_file_manager_factory)
      : time_source_(time_source), async_file_manager_factory_(async_file_manager_factory) {}

  std::shared_ptr<FileSystemHttpCache> get(std::shared_ptr<CacheSingleton> singleton,
                                           const ConfigProto& config) {
    std::shared_ptr<FileSystemHttpCache> cache;
    auto& key = config.cache_path();
    absl::MutexLock lock(&mu_);
    auto it = caches_.find(key);
    if (it != caches_.end()) {
      cache = it->second.lock();
    }
    if (!cache) {
      cache = std::make_shared<FileSystemHttpCache>(
          singleton, config, time_source_,
          async_file_manager_factory_->getAsyncFileManager(config.manager_config()));
      cache->populateFromDisk();
      caches_[key] = cache;
    } else if (!Protobuf::util::MessageDifferencer::Equals(cache->config(), config)) {
      throw EnvoyException(
          fmt::format("mismatched FileSystemHttpCacheConfig with same path\n{}\nvs.\n{}",
                      cache->config().DebugString(), config.DebugString()));
    }
    return cache;
  }

private:
  TimeSource& time_source_;
  std::shared_ptr<Common::AsyncFiles::AsyncFileManagerFactory> async_file_manager_factory_;
  absl::Mutex mu_;
  // We keep weak_ptr here so the caches can be destroyed if the config is updated to stop using
  // that config of cache. The caches each keep shared_ptrs to this singleton, which keeps the
  // singleton from being destroyed unless it's no longer keeping track of any caches.
  // (The singleton shared_ptr is *only* held by cache instances.)
  absl::flat_hash_map<std::string, std::weak_ptr<FileSystemHttpCache>> caches_ ABSL_GUARDED_BY(mu_);
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
  std::shared_ptr<HttpCache>
  getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& filter_config,
           Server::Configuration::FactoryContext& context) override {
    ConfigProto config;
    MessageUtil::unpackTo(filter_config.typed_config(), config);
    std::shared_ptr<CacheSingleton> caches = context.singletonManager().getTyped<CacheSingleton>(
        SINGLETON_MANAGER_REGISTERED_NAME(file_system_http_cache_singleton), [&context] {
          return std::make_shared<CacheSingleton>(
              context.timeSource(),
              Common::AsyncFiles::AsyncFileManagerFactory::singleton(&context.singletonManager()));
        });
    return caches->get(caches, config);
  }
};

static Registry::RegisterFactory<FileSystemHttpCacheFactory, HttpCacheFactory> register_;

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy