#pragma once

#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.h"
#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SourceIpAccess {

/**
 * Config registration for the source ip access filter. @see NamedNetworkFilterConfigFactory.
 */
class SourceIpAccessConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::source_ip_access::v2::SourceIpAccess> {
public:
  SourceIpAccessConfigFactory() : FactoryBase(NetworkFilterNames::get().SourceIpAccess) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::source_ip_access::v2::SourceIpAccess& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace SourceIpAccess
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
