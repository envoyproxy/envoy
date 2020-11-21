#include "extensions/filters/http/health_check/health_check.h"

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/header_map.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

struct RcDetailsValues {
  // The health check filter returned healthy to a health check.
  const std::string HealthCheckOk = "health_check_ok";
  // The health check filter responded with a failed health check.
  const std::string HealthCheckFailed = "health_check_failed";
  // The health check filter returned a cached health value.
  const std::string HealthCheckCached = "health_check_cached";
  // The health check filter failed due to health checking a nonexistent cluster.
  const std::string HealthCheckNoCluster = "health_check_failed_no_cluster_found";
  // The health check filter failed due to checking min_degraded against an empty cluster.
  const std::string HealthCheckClusterEmpty = "health_check_failed_cluster_empty";
  // The health check filter succeeded given the cluster health was sufficient.
  const std::string HealthCheckClusterHealthy = "health_check_ok_cluster_healthy";
  // The health check filter failed given the cluster health was not sufficient.
  const std::string HealthCheckClusterUnhealthy = "health_check_failed_cluster_unhealthy";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

HealthCheckCacheManager::HealthCheckCacheManager(Event::Dispatcher& dispatcher,
                                                 std::chrono::milliseconds timeout)
    : clear_cache_timer_(dispatcher.createTimer([this]() -> void { onTimer(); })),
      timeout_(timeout) {
  onTimer();
}

void HealthCheckCacheManager::onTimer() {
  use_cached_response_ = false;
  clear_cache_timer_->enableTimer(timeout_);
}

Http::FilterHeadersStatus HealthCheckFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                           bool end_stream) {
  if (Http::HeaderUtility::matchHeaders(headers, *header_match_data_)) {
    health_check_request_ = true;
    callbacks_->streamInfo().healthCheck(true);

    // Set the 'sampled' status for the span to false. This overrides
    // any previous sampling decision associated with the trace instance,
    // resulting in this span (and any subsequent child spans) not being
    // reported to the backend tracing system.
    callbacks_->activeSpan().setSampled(false);

    // If we are not in pass through mode, we always handle. Otherwise, we handle if the server is
    // in the failed state or if we are using caching and we should use the cached response.
    if (!pass_through_mode_ || context_.healthCheckFailed() ||
        (cache_manager_ && cache_manager_->useCachedResponse())) {
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

Http::FilterTrailersStatus HealthCheckFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterTrailersStatus::StopIteration
                   : Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus HealthCheckFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (health_check_request_) {
    if (cache_manager_) {
      cache_manager_->setCachedResponse(
          static_cast<Http::Code>(Http::Utility::getResponseStatus(headers)),
          headers.EnvoyDegraded() != nullptr);
    }

    headers.setEnvoyUpstreamHealthCheckedCluster(context_.localInfo().clusterName());
  } else if (context_.healthCheckFailed()) {
    headers.setReferenceEnvoyImmediateHealthCheckFail(
        Http::Headers::get().EnvoyImmediateHealthCheckFailValues.True);
  }

  return Http::FilterHeadersStatus::Continue;
}

void HealthCheckFilter::onComplete() {
  ASSERT(handling_);
  Http::Code final_status = Http::Code::OK;
  const std::string* details = &RcDetails::get().HealthCheckOk;
  bool degraded = false;
  if (context_.healthCheckFailed()) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FailedLocalHealthCheck);
    final_status = Http::Code::ServiceUnavailable;
    details = &RcDetails::get().HealthCheckFailed;
  } else {
    if (cache_manager_) {
      const auto status_and_degraded = cache_manager_->getCachedResponse();
      final_status = status_and_degraded.first;
      details = &RcDetails::get().HealthCheckCached;
      degraded = status_and_degraded.second;
    } else if (cluster_min_healthy_percentages_ != nullptr &&
               !cluster_min_healthy_percentages_->empty()) {
      // Check the status of the specified upstream cluster(s) to determine the right response.
      auto& clusterManager = context_.clusterManager();
      for (const auto& item : *cluster_min_healthy_percentages_) {
        details = &RcDetails::get().HealthCheckClusterHealthy;
        const std::string& cluster_name = item.first;
        const uint64_t min_healthy_percentage = static_cast<uint64_t>(item.second);
        auto* cluster = clusterManager.get(cluster_name);
        if (cluster == nullptr) {
          // If the cluster does not exist at all, consider the service unhealthy.
          final_status = Http::Code::ServiceUnavailable;
          details = &RcDetails::get().HealthCheckNoCluster;

          break;
        }
        const auto& stats = cluster->info()->stats();
        const uint64_t membership_total = stats.membership_total_.value();
        if (membership_total == 0) {
          // If the cluster exists but is empty, consider the service unhealthy unless
          // the specified minimum percent healthy for the cluster happens to be zero.
          if (min_healthy_percentage == 0UL) {
            continue;
          } else {
            final_status = Http::Code::ServiceUnavailable;
            details = &RcDetails::get().HealthCheckClusterEmpty;
            break;
          }
        }
        // In the general case, consider the service unhealthy if fewer than the
        // specified percentage of the servers in the cluster are available (healthy + degraded).
        if ((100UL * (stats.membership_healthy_.value() + stats.membership_degraded_.value())) <
            membership_total * min_healthy_percentage) {
          final_status = Http::Code::ServiceUnavailable;
          details = &RcDetails::get().HealthCheckClusterUnhealthy;
          break;
        }
      }
    }

    if (!Http::CodeUtility::is2xx(enumToInt(final_status))) {
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FailedLocalHealthCheck);
    }
  }

  callbacks_->sendLocalReply(
      final_status, "",
      [degraded](auto& headers) {
        if (degraded) {
          headers.setEnvoyDegraded("");
        }
      },
      absl::nullopt, *details);
}

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
