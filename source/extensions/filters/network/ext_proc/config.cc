#include "source/extensions/filters/network/ext_proc/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

Network::FilterFactoryCb NetworkExtProcConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& proto_config,
    Server::Configuration::FactoryContext&) {
  ConfigSharedPtr ext_proc_config = std::make_shared<Config>(proto_config);

  return [ext_proc_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<NetworkExtProcFilter>(ext_proc_config));
  };
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
REGISTER_FACTORY(NetworkExtProcConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
