#include "extensions/filters/http/health_check/config.h"

#include <memory>

#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.h"
#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/http/header_utility.h"
#include "common/http/headers.h"

#include "extensions/filters/http/health_check/health_check.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

Http::FilterFactoryCb HealthCheckFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::health_check::v3::HealthCheck& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_pass_through_mode());

  const bool pass_through_mode = proto_config.pass_through_mode().value();
  const int64_t cache_time_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, cache_time, 0);

  auto header_match_data = std::make_shared<std::vector<Http::HeaderUtility::HeaderDataPtr>>();
  *header_match_data = Http::HeaderUtility::buildHeaderDataVector(proto_config.headers());

  if (!pass_through_mode && cache_time_ms) {
    throw EnvoyException("cache_time_ms must not be set when path_through_mode is disabled");
  }

  HealthCheckCacheManagerSharedPtr cache_manager;
  if (cache_time_ms > 0) {
    cache_manager = std::make_shared<HealthCheckCacheManager>(
        context.dispatcher(), std::chrono::milliseconds(cache_time_ms));
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
 * Static registration for the health check filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HealthCheckFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.health_check"};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
