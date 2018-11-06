#pragma once

#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Config registration for the dubbo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class DubboProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy> {
public:
  DubboProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().DubboProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
