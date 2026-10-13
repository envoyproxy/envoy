#include "library/common/extensions/filters/http/network_configuration/config.h"

#include "source/server/generic_factory_context.h"

#include "library/common/extensions/filters/http/network_configuration/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

absl::StatusOr<Http::FilterFactoryCb>
NetworkConfigurationFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {

  Server::GenericFactoryContextImpl generic_context(
      context, extra_context.scope, extra_context.visitor, extra_context.init_manager);
  auto connectivity_manager = Network::ConnectivityManagerFactory{generic_context}.get();
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
