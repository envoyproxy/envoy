#include "extensions/filters/network/postgresql_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

/**
 * Config registration for the PostgreSQL proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb
NetworkFilters::PostgreSQLProxy::PostgreSQLConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::postgresql_proxy::v3alpha::PostgreSQLProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("postgresql.{}", proto_config.stat_prefix());

  PostgreSQLFilterConfigSharedPtr filter_config(
      std::make_shared<PostgreSQLFilterConfig>(stat_prefix, context.scope()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<PostgreSQLFilter>(filter_config));
  };
}

/**
 * Static registration for the PostgreSQL proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PostgreSQLConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
