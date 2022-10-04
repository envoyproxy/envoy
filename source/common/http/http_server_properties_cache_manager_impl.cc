#include "source/common/http/http_server_properties_cache_manager_impl.h"

#include "envoy/common/key_value_store.h"
#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

SINGLETON_MANAGER_REGISTRATION(alternate_protocols_cache_manager);

HttpServerPropertiesCacheManagerImpl::HttpServerPropertiesCacheManagerImpl(
    AlternateProtocolsData& data, ThreadLocal::SlotAllocator& tls)
    : data_(data), slot_(tls) {
  slot_.set([](Event::Dispatcher& /*dispatcher*/) { return std::make_shared<State>(); });
}

HttpServerPropertiesCacheSharedPtr HttpServerPropertiesCacheManagerImpl::getCache(
    const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
    Event::Dispatcher& dispatcher) {
  if (options.has_key_value_store_config() && data_.concurrency_ != 1) {
    throw EnvoyException(
        fmt::format("options has key value store but Envoy has concurrency = {} : {}",
                    data_.concurrency_, options.DebugString()));
  }

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

  std::unique_ptr<KeyValueStore> store;
  if (options.has_key_value_store_config()) {
    envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
    MessageUtil::anyConvertAndValidate(options.key_value_store_config().typed_config(), kv_config,
                                       data_.validation_visitor_);
    auto& factory = Config::Utility::getAndCheckFactory<KeyValueStoreFactory>(kv_config.config());
    store =
        factory.createStore(kv_config, data_.validation_visitor_, dispatcher, data_.file_system_);
  }

  std::vector<std::string> canonical_suffixes;
  for (const std::string& suffix : options.canonical_suffixes()) {
    if (!absl::StartsWith(suffix, ".")) {
      IS_ENVOY_BUG(absl::StrCat("Suffix does not start with a leading '.': ", suffix));
      continue;
    }
    canonical_suffixes.push_back(suffix);
  }

  auto new_cache = std::make_shared<HttpServerPropertiesCacheImpl>(
      dispatcher, std::move(canonical_suffixes), std::move(store), options.max_entries().value());

  for (const envoy::config::core::v3::AlternateProtocolsCacheOptions::AlternateProtocolsCacheEntry&
           entry : options.prepopulated_entries()) {
    const HttpServerPropertiesCacheImpl::Origin origin = {"https", entry.hostname(), entry.port()};
    std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocol = {
        {"h3", entry.hostname(), entry.port(),
         dispatcher.timeSource().monotonicTime() + std::chrono::hours(168)}};
    OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> existing_protocols =
        new_cache->findAlternatives(origin);
    if (!existing_protocols.has_value()) {
      new_cache->setAlternatives(origin, protocol);
    }
  }

  (*slot_).caches_.emplace(options.name(), CacheWithOptions{options, new_cache});
  return new_cache;
}

HttpServerPropertiesCacheManagerSharedPtr HttpServerPropertiesCacheManagerFactoryImpl::get() {
  return singleton_manager_.getTyped<HttpServerPropertiesCacheManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(alternate_protocols_cache_manager),
      [this] { return std::make_shared<HttpServerPropertiesCacheManagerImpl>(data_, tls_); });
}

} // namespace Http
} // namespace Envoy
