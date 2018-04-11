#include "extensions/filters/http/health_check/config.h"

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/health_check/health_check.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

Server::Configuration::HttpFilterFactoryCb HealthCheckFilterConfig::createFilter(
    const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_pass_through_mode());
  ASSERT(!proto_config.endpoint().empty());

  const bool pass_through_mode = proto_config.pass_through_mode().value();
  const int64_t cache_time_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, cache_time, 0);
  const std::string hc_endpoint = proto_config.endpoint();

  if (!pass_through_mode && cache_time_ms) {
    throw EnvoyException("cache_time_ms must not be set when path_through_mode is disabled");
  }

  HealthCheckCacheManagerSharedPtr cache_manager;
  if (cache_time_ms > 0) {
    cache_manager.reset(new HealthCheckCacheManager(context.dispatcher(),
                                                    std::chrono::milliseconds(cache_time_ms)));
  }

  ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages;
  if (!pass_through_mode && !proto_config.cluster_min_healthy_percentages().empty()) {
    auto cluster_to_percentage = std::make_unique<ClusterMinHealthyPercentages>();
    for (const auto& item : proto_config.cluster_min_healthy_percentages()) {
      cluster_to_percentage->emplace(std::make_pair(item.first, item.second.value()));
    }
    cluster_min_healthy_percentages = std::move(cluster_to_percentage);
  }

  return [&context, pass_through_mode, cache_manager, hc_endpoint,
          cluster_min_healthy_percentages](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<HealthCheckFilter>(
        context, pass_through_mode, cache_manager, hc_endpoint, cluster_min_healthy_percentages));

  };
}

/**
 * Config registration for the health check filter. @see NamedHttpFilterConfigFactory.
 */
Server::Configuration::HttpFilterFactoryCb
HealthCheckFilterConfig::createFilterFactory(const Json::Object& json_config,
                                             const std::string& stats_prefix,
                                             Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  Config::FilterJson::translateHealthCheckFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb HealthCheckFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  return createFilter(
      dynamic_cast<const envoy::config::filter::http::health_check::v2::HealthCheck&>(proto_config),
      stats_prefix, context);
}

/**
 * Static registration for the health check filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<HealthCheckFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
