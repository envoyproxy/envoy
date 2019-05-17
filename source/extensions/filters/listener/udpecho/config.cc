#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/udpecho/echo.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace UdpEcho {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoConfigFactory : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_unique<EchoFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return ListenerFilterNames::get().UdpEcho; }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
REGISTER_FACTORY(EchoConfigFactory, Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace UdpEcho
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
