#include "extensions/filters/http/health_check/config.h"

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"

#include "extensions/filters/http/health_check/health_check.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

Http::FilterFactoryCb HealthCheckFilterConfig::createFilter(
    const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_pass_through_mode());

  const bool pass_through_mode = proto_config.pass_through_mode().value();
  const int64_t cache_time_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, cache_time, 0);
  const std::string hc_endpoint = proto_config.endpoint();

  auto header_match_data = std::make_shared<std::vector<Http::HeaderUtility::HeaderData>>();

  // TODO(mrice32): remove endpoint field at the end of the 1.7.0 deprecation cycle.
  const bool endpoint_set = !proto_config.endpoint().empty();
  if (endpoint_set) {
    envoy::api::v2::route::HeaderMatcher matcher;
    matcher.set_name(Http::Headers::get().Path.get());
    matcher.set_exact_match(proto_config.endpoint());
    header_match_data->emplace_back(matcher);
  }

  for (const envoy::api::v2::route::HeaderMatcher& matcher : proto_config.headers()) {
    Http::HeaderUtility::HeaderData single_header_match(matcher);
    // Ignore any path header matchers if the endpoint field has been set.
    if (!(endpoint_set && single_header_match.name_ == Http::Headers::get().Path)) {
      header_match_data->push_back(std::move(single_header_match));
    }
  }

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

  return [&context, pass_through_mode, cache_manager, header_match_data,
          cluster_min_healthy_percentages](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<HealthCheckFilter>(context, pass_through_mode,
                                                                  cache_manager, header_match_data,
                                                                  cluster_min_healthy_percentages));

  };
}

/**
 * Config registration for the health check filter. @see NamedHttpFilterConfigFactory.
 */
Http::FilterFactoryCb
HealthCheckFilterConfig::createFilterFactory(const Json::Object& json_config,
                                             const std::string& stats_prefix,
                                             Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  Config::FilterJson::translateHealthCheckFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

Http::FilterFactoryCb HealthCheckFilterConfig::createFilterFactoryFromProto(
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
