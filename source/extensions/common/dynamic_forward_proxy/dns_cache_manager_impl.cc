#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

SINGLETON_MANAGER_REGISTRATION(dns_cache_manager);

absl::StatusOr<DnsCacheSharedPtr> DnsCacheManagerImpl::getCache(
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) {
  const auto& existing_cache = caches_.find(config.name());
  if (existing_cache != caches_.end()) {
    if (!Protobuf::util::MessageDifferencer::Equivalent(config, existing_cache->second.config_)) {
      return absl::InvalidArgumentError(
          fmt::format("config specified DNS cache '{}' with different settings", config.name()));
    }

    return existing_cache->second.cache_;
  }

  auto cache_or_status = DnsCacheImpl::createDnsCacheImpl(context_, config);
  RETURN_IF_NOT_OK_REF(cache_or_status.status());
  DnsCacheSharedPtr new_cache = std::move(cache_or_status.value());
  caches_.emplace(config.name(), ActiveCache{config, new_cache});
  return new_cache;
}

DnsCacheSharedPtr DnsCacheManagerImpl::lookUpCacheByName(absl::string_view cache_name) {
  ASSERT(context_.serverFactoryContext().mainThreadDispatcher().isThreadSafe());
  const auto& existing_cache = caches_.find(cache_name);
  if (existing_cache != caches_.end()) {
    return existing_cache->second.cache_;
  }

  return nullptr;
}

DnsCacheManagerSharedPtr DnsCacheManagerFactoryImpl::get() {
  return context_.serverFactoryContext().singletonManager().getTyped<DnsCacheManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(dns_cache_manager),
      [this] { return std::make_shared<DnsCacheManagerImpl>(context_); });
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
