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

HttpServerPropertiesCacheManagerImpl::HttpServerPropertiesCacheManagerImpl(
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor, ThreadLocal::SlotAllocator& tls)
    : data_(context, validation_visitor), slot_(tls) {
  slot_.set([](Event::Dispatcher& /*dispatcher*/) { return std::make_shared<State>(); });
}

HttpServerPropertiesCacheSharedPtr HttpServerPropertiesCacheManagerImpl::getCache(
    const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
    Event::Dispatcher& dispatcher) {
  const auto& existing_cache = (*slot_).caches_.find(options.name());
  if (existing_cache != (*slot_).caches_.end()) {
    if (!Protobuf::util::MessageDifferencer::Equivalent(options, existing_cache->second.options_)) {
      IS_ENVOY_BUG(fmt::format(
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
        {"h3", "", entry.port(),
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

void HttpServerPropertiesCacheManagerImpl::forEachThreadLocalCache(CacheFn cache_fn) {
  for (auto& entry : (*slot_).caches_) {
    HttpServerPropertiesCache& cache = *entry.second.cache_;
    cache_fn(cache);
  }
}

} // namespace Http
} // namespace Envoy
