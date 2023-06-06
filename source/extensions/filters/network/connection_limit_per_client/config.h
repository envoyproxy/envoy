#pragma once

#include "envoy/extensions/filters/network/connection_limit_per_client/v3/connection_limit_per_client.pb.h"
#include "envoy/extensions/filters/network/connection_limit_per_client/v3/connection_limit_per_client.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitPerClientFilter {

/**
 * Config registration for the connection limit per client filter. @see NamedNetworkFilterConfigFactory.
 */
class ConnectionLimitPerClientConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::connection_limit_per_client::v3::ConnectionLimitPerClient> {
public:
  ConnectionLimitPerClientConfigFactory() : FactoryBase(NetworkFilterNames::get().ConnectionLimitPerClient) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::connection_limit_per_client::v3::ConnectionLimitPerClient&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ConnectionLimitPerClientFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
