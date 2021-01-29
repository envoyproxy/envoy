#include "extensions/filters/network/mysql_proxy/mysql_config.h"

#include <string>

#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Config registration for the MySQL proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb
NetworkFilters::MySQLProxy::MySQLConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("mysql.{}", proto_config.stat_prefix());

  MySQLFilterConfigSharedPtr filter_config(
      std::make_shared<MySQLFilterConfig>(stat_prefix, context.scope()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<MySQLFilter>(filter_config));
  };
}

/**
 * Static registration for the MySQL proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MySQLConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
