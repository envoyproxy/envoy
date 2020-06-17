#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "common/protobuf/protobuf.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

SINGLETON_MANAGER_REGISTRATION(dns_cache_manager);

DnsCacheSharedPtr DnsCacheManagerImpl::getCache(
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) {
  const auto& existing_cache = caches_.find(config.name());
  if (existing_cache != caches_.end()) {
    if (!Protobuf::util::MessageDifferencer::Equivalent(config, existing_cache->second.config_)) {
      throw EnvoyException(
          fmt::format("config specified DNS cache '{}' with different settings", config.name()));
    }

    return existing_cache->second.cache_;
  }

  DnsCacheSharedPtr new_cache = std::make_shared<DnsCacheImpl>(
      main_thread_dispatcher_, tls_, random_, loader_, root_scope_, config);
  caches_.emplace(config.name(), ActiveCache{config, new_cache});
  return new_cache;
}

DnsCacheManagerSharedPtr getCacheManager(Singleton::Manager& singleton_manager,
                                         Event::Dispatcher& main_thread_dispatcher,
                                         ThreadLocal::SlotAllocator& tls,
                                         Runtime::RandomGenerator& random, Runtime::Loader& loader,
                                         Stats::Scope& root_scope) {
  return singleton_manager.getTyped<DnsCacheManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(dns_cache_manager),
      [&main_thread_dispatcher, &tls, &random, &loader, &root_scope] {
        return std::make_shared<DnsCacheManagerImpl>(main_thread_dispatcher, tls, random, loader,
                                                     root_scope);
      });
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
