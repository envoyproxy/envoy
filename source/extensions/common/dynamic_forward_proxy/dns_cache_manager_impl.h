#pragma once

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/server/factory_context_base_impl.h"
#include "source/server/generic_factory_context.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DnsCacheManagerImpl : public DnsCacheManager, public Singleton::Instance {
public:
  DnsCacheManagerImpl(const Server::Configuration::GenericFactoryContext& context)
      : context_(context) {}

  // DnsCacheManager
  absl::StatusOr<DnsCacheSharedPtr> getCache(
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) override;
  DnsCacheSharedPtr lookUpCacheByName(absl::string_view cache_name) override;

private:
  struct ActiveCache {
    ActiveCache(const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
                DnsCacheSharedPtr cache)
        : config_(config), cache_(cache) {}

    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
    DnsCacheSharedPtr cache_;
  };

  Server::GenericFactoryContextImpl context_;
  absl::flat_hash_map<std::string, ActiveCache> caches_;
};

class DnsCacheManagerFactoryImpl : public DnsCacheManagerFactory {
public:
  DnsCacheManagerFactoryImpl(Server::Configuration::ServerFactoryContext& server_context,
                             ProtobufMessage::ValidationVisitor& validation_visitor)
      : context_(server_context, validation_visitor) {}
  DnsCacheManagerFactoryImpl(Server::GenericFactoryContextImpl context) : context_(context) {}

  DnsCacheManagerSharedPtr get() override;

private:
  Server::GenericFactoryContextImpl context_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
