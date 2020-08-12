#include "extensions/filters/network/postgres_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * Config registration for the Postgres proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb
NetworkFilters::PostgresProxy::PostgresConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("postgres.{}", proto_config.stat_prefix());
  const bool enable_sql = PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, enable_sql_parsing, true);

  PostgresFilterConfigSharedPtr filter_config(
      std::make_shared<PostgresFilterConfig>(stat_prefix, enable_sql, context.scope()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<PostgresFilter>(filter_config));
  };
}

/**
 * Static registration for the Postgres proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PostgresConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
