#include "source/extensions/filters/network/tunnel_hostname_lookup/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/common/synthetic_ip/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TunnelHostnameLookup {

absl::StatusOr<Network::FilterFactoryCb>
TunnelHostnameLookupConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) {

  auto config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::network::tunnel_hostname_lookup::v3::TunnelHostnameLookup&>(
      proto_config, context.messageValidationVisitor());

  auto filter_config = std::make_shared<TunnelHostnameLookupConfig>(config);

  // Get or create singleton cache manager
  // This should be the same instance created by the DNS Gateway filter
  auto cache_manager = context.serverFactoryContext()
                           .singletonManager()
                           .getTyped<Common::SyntheticIp::SyntheticIpCacheManager>(
                               "synthetic_ip_cache_manager_singleton", []() {
                                 // This should never be called if DNS Gateway filter is configured
                                 // first Return nullptr - we'll check below and return proper error
                                 // status
                                 return nullptr;
                               });

  if (!cache_manager) {
    return absl::InvalidArgumentError(
        "Synthetic IP cache manager not initialized. "
        "Ensure DNS Gateway filter is configured before this filter.");
  }

  return [filter_config, cache_manager](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<TunnelHostnameLookupFilter>(filter_config, cache_manager));
  };
}

ProtobufTypes::MessagePtr TunnelHostnameLookupConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::filters::network::tunnel_hostname_lookup::v3::TunnelHostnameLookup>();
}

std::string TunnelHostnameLookupConfigFactory::name() const {
  return "envoy.filters.network.tunnel_hostname_lookup";
}

/**
 * Static registration for the Tunnel Hostname Lookup filter config factory.
 */
REGISTER_FACTORY(TunnelHostnameLookupConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace TunnelHostnameLookup
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
