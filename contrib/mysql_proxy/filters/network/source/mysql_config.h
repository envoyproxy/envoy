#pragma once

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.validate.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Config registration for the MySQL proxy filter.
 */
class MySQLConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy> {
public:
  MySQLConfigFactory() : FactoryBase(NetworkFilterNames::get().MySQLProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
