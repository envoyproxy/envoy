#include "library/common/extensions/filters/http/network_configuration/config.h"

#include "library/common/extensions/filters/http/network_configuration/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

Http::FilterFactoryCb NetworkConfigurationFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  auto connectivity_manager = Network::ConnectivityManagerFactory{context}.get();
  bool enable_drain_post_dns_refresh = proto_config.enable_drain_post_dns_refresh();
  bool enable_interface_binding = proto_config.enable_interface_binding();

  return [connectivity_manager, enable_drain_post_dns_refresh,
          enable_interface_binding](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<NetworkConfigurationFilter>(
        connectivity_manager, enable_drain_post_dns_refresh, enable_interface_binding));
  };
}

/**
 * Static registration for the NetworkConfiguration filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(NetworkConfigurationFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
