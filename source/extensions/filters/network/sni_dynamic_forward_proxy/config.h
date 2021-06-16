#pragma once

#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3alpha/sni_dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3alpha/sni_dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

constexpr char SniDynamicForwardProxyName[] = "envoy.filters.network.sni_dynamic_forward_proxy";

using FilterConfig =
    envoy::extensions::filters::network::sni_dynamic_forward_proxy::v3alpha::FilterConfig;

/**
 * Config registration for the sni_dynamic_forward_proxy filter. @see
 * NamedNetworkFilterConfigFactory.
 */
class SniDynamicForwardProxyNetworkFilterConfigFactory : public Common::FactoryBase<FilterConfig> {
public:
  SniDynamicForwardProxyNetworkFilterConfigFactory();

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
