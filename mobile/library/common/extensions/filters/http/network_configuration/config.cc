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
  if (connectivity_manager != nullptr) {
    connectivity_manager->setInterfaceBindingEnabled(proto_config.enable_interface_binding());
    connectivity_manager->setDrainPostDnsRefreshEnabled(
        proto_config.enable_drain_post_dns_refresh());
  }

  return [connectivity_manager](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<NetworkConfigurationFilter>(connectivity_manager));
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
