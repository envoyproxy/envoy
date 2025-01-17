#pragma once

#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3/sni_dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3/sni_dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

using FilterConfig =
    envoy::extensions::filters::network::sni_dynamic_forward_proxy::v3::FilterConfig;

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
