#include "source/extensions/upstreams/http/dynamic_modules/config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

namespace {

// Thread-local cache of BridgeConfig instances keyed by proto config hash.
// This ensures that on_bridge_config_new is called once per unique configuration per
// worker thread, matching the lifecycle semantics of other dynamic module extension points.
thread_local absl::flat_hash_map<uint64_t, BridgeConfigSharedPtr> bridge_config_cache;

absl::StatusOr<BridgeConfigSharedPtr> getOrCreateBridgeConfig(
    const envoy::extensions::upstreams::http::dynamic_modules::v3::Config& proto_config) {
  const uint64_t cache_key = MessageUtil::hash(proto_config);
  auto it = bridge_config_cache.find(cache_key);
  if (it != bridge_config_cache.end()) {
    return it->second;
  }

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string bridge_config_bytes;
  if (proto_config.has_bridge_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.bridge_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError("failed to parse bridge_config: " +
                                        std::string(config_or_error.status().message()));
    }
    bridge_config_bytes = std::move(config_or_error.value());
  }

  auto bridge_config = BridgeConfig::create(proto_config.bridge_name(), bridge_config_bytes,
                                            std::move(dynamic_module.value()));
  if (!bridge_config.ok()) {
    return bridge_config.status();
  }

  bridge_config_cache[cache_key] = bridge_config.value();
  return bridge_config.value();
}

} // namespace

Router::GenericConnPoolPtr DynamicModuleGenericConnPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
    Router::GenericConnPoolFactory::UpstreamProtocol, Upstream::ResourcePriority priority,
    absl::optional<Envoy::Http::Protocol>, Upstream::LoadBalancerContext* ctx,
    const Protobuf::Message& config) const {
  const auto& typed_config =
      dynamic_cast<const envoy::extensions::upstreams::http::dynamic_modules::v3::Config&>(config);

  auto bridge_config = getOrCreateBridgeConfig(typed_config);
  if (!bridge_config.ok()) {
    ENVOY_LOG_MISC(error, "failed to create bridge config: {}", bridge_config.status().message());
    return nullptr;
  }

  auto ret = std::make_unique<TcpConnPool>(host, thread_local_cluster, priority, ctx,
                                           bridge_config.value());
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(DynamicModuleGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
