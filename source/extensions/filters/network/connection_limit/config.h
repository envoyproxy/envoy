#pragma once

#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.h"
#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitFilter {

/**
 * Config registration for the connection limit filter. @see NamedNetworkFilterConfigFactory.
 */
class ConnectionLimitConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit> {
public:
  ConnectionLimitConfigFactory() : FactoryBase(NetworkFilterNames::get().ConnectionLimit) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ConnectionLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
