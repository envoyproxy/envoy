#include "contrib/postgres_proxy/filters/network/source/config.h"

#include "envoy/common/exception.h"

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
  config_options.stats_prefix_ = fmt::format("postgres.{}", proto_config.stat_prefix());
  config_options.enable_sql_parsing_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, enable_sql_parsing, true);
  config_options.terminate_ssl_ = proto_config.terminate_ssl();
  config_options.upstream_ssl_ = proto_config.upstream_ssl();
  config_options.downstream_ssl_ = proto_config.downstream_ssl();

  if (config_options.terminate_ssl_ && proto_config.has_downstream_ssl() &&
      config_options.downstream_ssl_ ==
          envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::DISABLE) {
    throw EnvoyException("terminate_ssl cannot be set to true at the same time when downstream_ssl "
                         "is set to DISABLE");
  }

  PostgresFilterConfigSharedPtr filter_config(
      std::make_shared<PostgresFilterConfig>(config_options, context.scope()));
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
