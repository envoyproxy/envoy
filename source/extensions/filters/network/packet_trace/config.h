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

/**
 * Config registration for the packet_packet filter. @see NamedNetworkFilterConfigFactory.
 */
class PacketTraceConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::packet_trace::v3::PacketTrace> {
public:
  PacketTraceConfigFactory() : FactoryBase(NetworkFilterNames::get().PacketTrace) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::packet_trace::v3::PacketTrace& config,
      Server::Configuration::FactoryContext&) override;

  bool isTerminalFilterByProtoTyped(
      const envoy::extensions::filters::network::packet_trace::v3::PacketTrace&,
      Server::Configuration::FactoryContext&) override {
    return false;
  }
};

} // namespace PacketTrace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
