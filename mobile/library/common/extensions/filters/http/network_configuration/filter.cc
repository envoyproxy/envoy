#include "library/common/extensions/filters/http/network_configuration/filter.h"

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

Http::FilterHeadersStatus NetworkConfigurationFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  ENVOY_LOG(debug, "NetworkConfigurationFilter::decodeHeaders");

  envoy_network_t network = network_configurator_->getPreferredNetwork();
  ENVOY_LOG(debug, "current preferred network: {}", network);

  if (enable_interface_binding_) {
    override_interface_ = network_configurator_->overrideInterface(network);
  }
  ENVOY_LOG(debug, "will override interface: {}", override_interface_);

  auto connection_options =
      network_configurator_->getUpstreamSocketOptions(network, override_interface_);
  decoder_callbacks_->addUpstreamSocketOptions(connection_options);

  return Http::FilterHeadersStatus::Continue;
}

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
