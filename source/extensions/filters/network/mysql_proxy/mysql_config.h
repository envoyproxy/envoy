#pragma once

#include "envoy/config/filter/network/mysql_proxy/v2/mysql_proxy.pb.h"
#include "envoy/config/filter/network/mysql_proxy/v2/mysql_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

/**
 * Config registration for the mysql proxy filter.
 */
class MysqlConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::mysql_proxy::v2::MysqlProxy> {
public:
  MysqlConfigFactory() : FactoryBase(NetworkFilterNames::get().MysqlProxy) {}

private:
  Network::FilterFactoryCb
      createFilterFactoryFromProtoTyped(const envoy::config::filter::network::mysql_proxy::v2::MysqlProxy& proto_config,
                                        Server::Configuration::FactoryContext& context) override;
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
