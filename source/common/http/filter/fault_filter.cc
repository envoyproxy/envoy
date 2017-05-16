#include "common/http/filter/fault_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Http {

FaultFilterConfig::FaultFilterConfig(const Json::Object& json_config, Runtime::Loader& runtime,
                                     const std::string& stat_prefix, Stats::Store& stats)
    : runtime_(runtime), stats_(generateStats(stat_prefix, stats)) {

  json_config.validateSchema(Json::Schema::FAULT_HTTP_FILTER_SCHEMA);

  const Json::ObjectPtr config_abort = json_config.getObject("abort", true);
  const Json::ObjectPtr config_delay = json_config.getObject("delay", true);

  if (config_abort->empty() && config_delay->empty()) {
    throw EnvoyException("fault filter must have at least abort or delay specified in the config.");
  }

  if (!config_abort->empty()) {
    abort_percent_ = static_cast<uint64_t>(config_abort->getInteger("abort_percent", 0));

    // TODO(mattklein123): Throw error if invalid return code is provided
    http_status_ = static_cast<uint64_t>(config_abort->getInteger("http_status"));
  }

  if (!config_delay->empty()) {
    const std::string type = config_delay->getString("type");
    ASSERT(type == "fixed");
    UNREFERENCED_PARAMETER(type);
    fixed_delay_percent_ =
        static_cast<uint64_t>(config_delay->getInteger("fixed_delay_percent", 0));
    fixed_duration_ms_ = static_cast<uint64_t>(config_delay->getInteger("fixed_duration_ms", 0));
  }

  if (json_config.hasObject("headers")) {
    std::vector<Json::ObjectPtr> config_headers = json_config.getObjectArray("headers");
    for (const Json::ObjectPtr& header_map : config_headers) {
      fault_filter_headers_.push_back(*header_map);
    }
  }

  upstream_cluster_ = json_config.getString("upstream_cluster", EMPTY_STRING);
}

FaultFilter::FaultFilter(FaultFilterConfigSharedPtr config) : config_(config) {}

FaultFilter::~FaultFilter() { ASSERT(!delay_timer_); }

// Delays and aborts are independent events. One can inject a delay
// followed by an abort or inject just a delay or abort. In this callback,
// if we inject a delay, then we will inject the abort in the delay timer
// callback.
FilterHeadersStatus FaultFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (!matchesTargetCluster()) {
    return FilterHeadersStatus::Continue;
  }

  // Check for header matches
  if (!Router::ConfigUtility::matchHeaders(headers, config_->filterHeaders())) {
    return FilterHeadersStatus::Continue;
  }

  if (config_->runtime().snapshot().featureEnabled("fault.http.delay.fixed_delay_percent",
                                                   config_->delayPercent())) {
    uint64_t duration_ms = config_->runtime().snapshot().getInteger(
        "fault.http.delay.fixed_duration_ms", config_->delayDuration());

    // Delay only if the duration is >0ms
    if (0 != duration_ms) {
      delay_timer_ =
          callbacks_->dispatcher().createTimer([this]() -> void { postDelayInjection(); });
      delay_timer_->enableTimer(std::chrono::milliseconds(duration_ms));
      config_->stats().delays_injected_.inc();
      callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::DelayInjected);
      return FilterHeadersStatus::StopIteration;
    }
  }

  if (config_->runtime().snapshot().featureEnabled("fault.http.abort.abort_percent",
                                                   config_->abortPercent())) {
    abortWithHTTPStatus();
    return FilterHeadersStatus::StopIteration;
  }

  return FilterHeadersStatus::Continue;
}

FilterDataStatus FaultFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus FaultFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

FaultFilterStats FaultFilterConfig::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = prefix + "fault.";
  return {ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(store, final_prefix))};
}

void FaultFilter::onDestroy() { resetTimerState(); }

void FaultFilter::postDelayInjection() {
  resetTimerState();
  // Delays can be followed by aborts
  if (config_->runtime().snapshot().featureEnabled("fault.http.abort.abort_percent",
                                                   config_->abortPercent())) {
    abortWithHTTPStatus();
  } else {
    // Continue request processing
    callbacks_->continueDecoding();
  }
}

void FaultFilter::abortWithHTTPStatus() {
  // TODO(mattklein123): check http status codes obtained from runtime
  Http::HeaderMapPtr response_headers{new HeaderMapImpl{
      {Headers::get().Status, std::to_string(config_->runtime().snapshot().getInteger(
                                  "fault.http.abort.http_status", config_->abortCode()))}}};
  callbacks_->encodeHeaders(std::move(response_headers), true);
  config_->stats().aborts_injected_.inc();
  callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::FaultInjected);
}

bool FaultFilter::matchesTargetCluster() {
  bool matches = true;

  if (!config_->upstreamCluster().empty()) {
    Router::RouteConstSharedPtr route = callbacks_->route();
    matches = route && route->routeEntry() &&
              (route->routeEntry()->clusterName() == config_->upstreamCluster());
  }

  return matches;
}

void FaultFilter::resetTimerState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

void FaultFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // Http
} // Envoy
