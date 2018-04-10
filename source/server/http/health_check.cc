#include "server/http/health_check.h"

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/config/filter_json.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb HealthCheckFilterConfig::createFilter(
    const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
    const std::string&, FactoryContext& context) {
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
HttpFilterFactoryCb HealthCheckFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                                 const std::string& stats_prefix,
                                                                 FactoryContext& context) {
  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  Config::FilterJson::translateHealthCheckFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

HttpFilterFactoryCb
HealthCheckFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                      const std::string& stats_prefix,
                                                      FactoryContext& context) {
  return createFilter(
      dynamic_cast<const envoy::config::filter::http::health_check::v2::HealthCheck&>(proto_config),
      stats_prefix, context);
}

/**
 * Static registration for the health check filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<HealthCheckFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server

HealthCheckCacheManager::HealthCheckCacheManager(Event::Dispatcher& dispatcher,
                                                 std::chrono::milliseconds timeout)
    : clear_cache_timer_(dispatcher.createTimer([this]() -> void { onTimer(); })),
      timeout_(timeout) {
  onTimer();
}

void HealthCheckCacheManager::onTimer() {
  use_cached_response_code_ = false;
  clear_cache_timer_->enableTimer(timeout_);
}

Http::FilterHeadersStatus HealthCheckFilter::decodeHeaders(Http::HeaderMap& headers,
                                                           bool end_stream) {
  if (headers.Path()->value() == endpoint_.c_str()) {
    health_check_request_ = true;
    callbacks_->requestInfo().healthCheck(true);

    // If we are not in pass through mode, we always handle. Otherwise, we handle if the server is
    // in the failed state or if we are using caching and we should use the cached response.
    if (!pass_through_mode_ || context_.healthCheckFailed() ||
        (cache_manager_ && cache_manager_->useCachedResponseCode())) {
      handling_ = true;
    }
  }

  if (end_stream && handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterHeadersStatus::StopIteration : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus HealthCheckFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream && handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterDataStatus::StopIterationNoBuffer
                   : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus HealthCheckFilter::decodeTrailers(Http::HeaderMap&) {
  if (handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterTrailersStatus::StopIteration
                   : Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus HealthCheckFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (health_check_request_) {
    if (cache_manager_) {
      cache_manager_->setCachedResponseCode(
          static_cast<Http::Code>(Http::Utility::getResponseStatus(headers)));
    }

    headers.insertEnvoyUpstreamHealthCheckedCluster().value(context_.localInfo().clusterName());
  } else if (context_.healthCheckFailed()) {
    headers.insertEnvoyImmediateHealthCheckFail().value(
        Http::Headers::get().EnvoyImmediateHealthCheckFailValues.True);
  }

  return Http::FilterHeadersStatus::Continue;
}

void HealthCheckFilter::onComplete() {
  ASSERT(handling_);
  Http::HeaderMapPtr headers;
  if (context_.healthCheckFailed()) {
    callbacks_->requestInfo().setResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck);
    headers.reset(new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::ServiceUnavailable))}});
  } else {
    Http::Code final_status = Http::Code::OK;
    if (cache_manager_) {
      final_status = cache_manager_->getCachedResponseCode();
    } else if (cluster_min_healthy_percentages_ != nullptr &&
               !cluster_min_healthy_percentages_->empty()) {
      // Check the status of the specified upstream cluster(s) to determine the right response.
      auto& clusterManager = context_.clusterManager();
      for (const auto& item : *cluster_min_healthy_percentages_) {
        const std::string& cluster_name = item.first;
        const double min_healthy_percentage = item.second;
        auto* cluster = clusterManager.get(cluster_name);
        if (cluster == nullptr) {
          // If the cluster does not exist at all, consider the service unhealthy.
          final_status = Http::Code::ServiceUnavailable;
          break;
        }
        const auto& stats = cluster->info()->stats();
        const uint64_t membership_total = stats.membership_total_.value();
        if (membership_total == 0) {
          // If the cluster exists but is empty, consider the service unhealty unless
          // the specified minimum percent healthy for the cluster happens to be zero.
          if (min_healthy_percentage == 0.0) {
            continue;
          } else {
            final_status = Http::Code::ServiceUnavailable;
            break;
          }
        }
        // In the general case, consider the service unhealthy if fewer than the
        // specified percentage of the servers in the cluster are healthy.
        // TODO(brian-pane) switch to purely integer-based math here, because the
        //                  int-to-float conversions and floating point division are slow.
        if (stats.membership_healthy_.value() < membership_total * min_healthy_percentage / 100.0) {
          final_status = Http::Code::ServiceUnavailable;
          break;
        }
      }
    }

    if (!Http::CodeUtility::is2xx(enumToInt(final_status))) {
      callbacks_->requestInfo().setResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck);
    }

    headers.reset(new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(final_status))}});
  }

  callbacks_->encodeHeaders(std::move(headers), true);
}

} // namespace Envoy
