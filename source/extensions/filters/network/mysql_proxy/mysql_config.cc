#include "extensions/filters/network/mysql_proxy/mysql_config.h"

#include <string>

#include "envoy/config/filter/network/mysql_proxy/v2/mysql_proxy.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

/**
 * Config registration for the mysql filter. @see
 * NamedNetworkFilterConfigFactory.
 */

Network::FilterFactoryCb
NetworkFilters::MysqlProxy::MysqlConfigFactory::createFilterFactoryFromProtoTyped(const envoy::config::filter::network::mysql_proxy::v2::MysqlProxy& proto_config, Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("mysql.{}.", proto_config.stat_prefix());

  MysqlFilterConfigSharedPtr filter_config(
      std::make_shared<MysqlFilterConfig>(stat_prefix, context.scope()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<MysqlFilter>(filter_config));
  };
}

/**
 * Static registration for the mysql filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<MysqlConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
