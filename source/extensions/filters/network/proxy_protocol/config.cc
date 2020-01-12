#include "extensions/filters/network/proxy_protocol/config.h"

#include "envoy/extensions/filters/network/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/proxy_protocol/proxy_protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ProxyProtocol {

Network::FilterFactoryCb ProxyProtocolConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::CommonFactoryContext&) {
  const auto config =
      dynamic_cast<const envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol&>(
          proto_config);
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addWriteFilter(std::make_shared<Filter>(config));
  };
}

ProtobufTypes::MessagePtr ProxyProtocolConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol>();
}

std::string ProxyProtocolConfigFactory::name() const {
  return NetworkFilterNames::get().ProxyProtocol;
}

/**
 * Static registration for the proxy protocol filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ProxyProtocolConfigFactory,
                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory);

} // namespace ProxyProtocol
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
