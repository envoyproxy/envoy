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

  PostgresFilterConfig::PostgresFilterConfigOptions config_options;
  config_options.stat_prefix_ = fmt::format("postgres.{}", proto_config.stat_prefix());
  config_options_.enable_sql_parsing_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, enable_sql_parsing, true);
  config_options_.terminate_ssl_ = proto_config.terminate_ssl();

  PostgresFilterConfigSharedPtr filter_config(std::make_shared<PostgresFilterConfig>(
      stat_prefix, enable_sql, terminate_ssl, context.scope()));
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
