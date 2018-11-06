#pragma once

#include "envoy/config/filter/network/mysql_proxy/v2/mysql_proxy.pb.h"
#include "envoy/config/filter/network/mysql_proxy/v2/mysql_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

#include "mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

static const std::string& MYSQL_FILTER() { CONSTRUCT_ON_FIRST_USE(std::string, "mysql"); }

/**
 * Config registration for the mysql proxy filter.
 */
class MysqlConfigFactory
    : public Extensions::NetworkFilters::Common::FactoryBase<mysql::MysqlProxy> {
public:
  MysqlConfigFactory() : FactoryBase(MYSQL_FILTER()) {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const mysql::MysqlProxy& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
