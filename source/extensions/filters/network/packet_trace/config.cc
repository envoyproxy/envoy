#include "source/extensions/filters/network/packet_trace/config.h"

#include "envoy/extensions/filters/network/packet_trace/v3/packet_trace.pb.h"
#include "envoy/extensions/filters/network/packet_trace/v3/packet_trace.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/packet_trace/packet_trace.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PacketTrace {

Network::FilterFactoryCb PacketTraceConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::packet_trace::v3::PacketTrace& config,
    Server::Configuration::FactoryContext&) {

  PacketTraceConfigSharedPtr filter_config(std::make_shared<PacketTraceConfig>(config));

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<PacketTraceFilter>(filter_config));
  };
}

/**
 * Static registration for the packet_trace filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PacketTraceConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace PacketTrace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
