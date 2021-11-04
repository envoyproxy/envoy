#pragma once

#include "envoy/extensions/filters/network/brpc_proxy/v3/brpc_proxy.pb.h"
#include "envoy/extensions/filters/network/brpc_proxy/v3/brpc_proxy.pb.validate.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

/**
 * Config registration for the brpc proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class BrpcProxyFilterConfigFactory: public Common::FactoryBase<
          envoy::extensions::filters::network::brpc_proxy::v3::BrpcProxy> {
public:
  BrpcProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().BrpcProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::brpc_proxy::v3::BrpcProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

