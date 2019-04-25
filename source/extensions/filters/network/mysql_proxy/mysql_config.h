#pragma once

#include "envoy/config/filter/network/mysql_proxy/v1alpha1/mysql_proxy.pb.h"
#include "envoy/config/filter/network/mysql_proxy/v1alpha1/mysql_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Config registration for the MySQL proxy filter.
 */
class MySQLConfigFactory : public Common::FactoryBase<
                               envoy::config::filter::network::mysql_proxy::v1alpha1::MySQLProxy> {
public:
  MySQLConfigFactory() : FactoryBase(NetworkFilterNames::get().MySQLProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::mysql_proxy::v1alpha1::MySQLProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
