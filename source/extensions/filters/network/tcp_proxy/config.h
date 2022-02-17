#pragma once

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

/**
 * Config registration for the tcp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class ConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy> {
public:
  ConfigFactory() : FactoryBase(NetworkFilterNames::get().TcpProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
