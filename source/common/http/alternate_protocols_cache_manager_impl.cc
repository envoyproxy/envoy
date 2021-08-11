#include "source/common/http/alternate_protocols_cache_manager_impl.h"

#include "source/common/http/alternate_protocols_cache_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

SINGLETON_MANAGER_REGISTRATION(alternate_protocols_cache_manager);

AlternateProtocolsCacheManagerImpl::AlternateProtocolsCacheManagerImpl(
    TimeSource& time_source, ThreadLocal::SlotAllocator& tls)
    : time_source_(time_source), slot_(tls) {
  slot_.set([](Event::Dispatcher& /*dispatcher*/) { return std::make_shared<State>(); });
}

AlternateProtocolsCacheSharedPtr AlternateProtocolsCacheManagerImpl::getCache(
    const envoy::config::core::v3::AlternateProtocolsCacheOptions& options) {
  const auto& existing_cache = (*slot_).caches_.find(options.name());
  if (existing_cache != (*slot_).caches_.end()) {
    if (!Protobuf::util::MessageDifferencer::Equivalent(options, existing_cache->second.options_)) {
      throw EnvoyException(fmt::format(
          "options specified alternate protocols cache '{}' with different settings"
          " first '{}' second '{}'",
          options.name(), existing_cache->second.options_.DebugString(), options.DebugString()));
    }

    return existing_cache->second.cache_;
  }

  AlternateProtocolsCacheSharedPtr new_cache =
      std::make_shared<AlternateProtocolsCacheImpl>(time_source_);
  (*slot_).caches_.emplace(options.name(), CacheWithOptions{options, new_cache});
  return new_cache;
}

AlternateProtocolsCacheManagerSharedPtr AlternateProtocolsCacheManagerFactoryImpl::get() {
  return singleton_manager_.getTyped<AlternateProtocolsCacheManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(alternate_protocols_cache_manager),
      [this] { return std::make_shared<AlternateProtocolsCacheManagerImpl>(time_source_, tls_); });
}

} // namespace Http
} // namespace Envoy
