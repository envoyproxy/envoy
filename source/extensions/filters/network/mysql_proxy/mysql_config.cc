#include "extensions/filters/network/mysql_proxy/mysql_config.h"

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/config/datasource.h"

#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/mysql_proxy/mysql_terminal_filter.h"
#include "extensions/filters/network/mysql_proxy/route.h"
#include "extensions/filters/network/mysql_proxy/route_impl.h"

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

  if (!proto_config.has_database_routes()) {
    return [filter_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(
          std::make_shared<MySQLMoniterFilter>(filter_config, DecoderFactoryImpl::instance_));
    };
  }

  absl::flat_hash_map<std::string, RouteSharedPtr> routes;
  RouteSharedPtr catch_all_route = nullptr;
  if (proto_config.has_database_routes()) {
    for (const auto& route : proto_config.database_routes().routes()) {
      routes.emplace(route.database(),
                     RouteFactoryImpl::instance.create(&context.clusterManager(), route.cluster()));
    }
    if (proto_config.database_routes().has_catch_all_route()) {
      catch_all_route = RouteFactoryImpl::instance.create(
          &context.clusterManager(), proto_config.database_routes().catch_all_route().cluster());
    }
  }

  auto router = std::make_shared<RouterImpl>(catch_all_route, std::move(routes));

  return [filter_config, router](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<MySQLTerminalFilter>(
        filter_config, router, DecoderFactoryImpl::instance_));
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
