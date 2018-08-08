#include "extensions/filters/network/source_ip_access/config.h"

#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/source_ip_access/source_ip_access.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SourceIpAccess {

Network::FilterFactoryCb SourceIpAccessConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::source_ip_access::v2::SourceIpAccess& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr source_ip_access_config(std::make_shared<Config>(proto_config, context.scope()));

  return [source_ip_access_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{std::make_shared<Filter>(source_ip_access_config)});
  };
}

/**
 * Static registration for the source ip access filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<SourceIpAccessConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace SourceIpAccess
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
