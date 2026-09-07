#pragma once

#include "envoy/extensions/filters/network/header_routing/v3/header_routing.pb.h"
#include "envoy/extensions/filters/network/header_routing/v3/header_routing.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HeaderRouting {

using FilterConfig = envoy::extensions::filters::network::header_routing::v3::HeaderRouting;

/**
 * Config registration for the header_routing network filter. @see
 * NamedNetworkFilterConfigFactory.
 */
class HeaderRoutingTcpFilterConfigFactory
    : public Common::FactoryBase<FilterConfig> {
public:
  HeaderRoutingTcpFilterConfigFactory();

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace HeaderRouting
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
