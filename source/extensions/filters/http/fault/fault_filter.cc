#include "source/extensions/filters/http/fault/fault_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

struct RcDetailsValues {
  // The fault filter injected an abort for this request.
  const std::string FaultAbort = "fault_filter_abort";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

FaultSettings::FaultSettings(const envoy::extensions::filters::http::fault::v3::HTTPFault& fault)
    : fault_filter_headers_(Http::HeaderUtility::buildHeaderDataVector(fault.headers())),
      delay_percent_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(fault, delay_percent_runtime,
                                                            RuntimeKeys::get().DelayPercentKey)),
      abort_percent_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(fault, abort_percent_runtime,
                                                            RuntimeKeys::get().AbortPercentKey)),
      delay_duration_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(fault, delay_duration_runtime,
                                                             RuntimeKeys::get().DelayDurationKey)),
      abort_http_status_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(
          fault, abort_http_status_runtime, RuntimeKeys::get().AbortHttpStatusKey)),
      abort_grpc_status_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(
          fault, abort_grpc_status_runtime, RuntimeKeys::get().AbortGrpcStatusKey)),
      max_active_faults_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(
          fault, max_active_faults_runtime, RuntimeKeys::get().MaxActiveFaultsKey)),
      response_rate_limit_percent_runtime_(
          PROTOBUF_GET_STRING_OR_DEFAULT(fault, response_rate_limit_percent_runtime,
                                         RuntimeKeys::get().ResponseRateLimitPercentKey)),
      disable_downstream_cluster_stats_(fault.disable_downstream_cluster_stats()) {
  if (fault.has_abort()) {
    request_abort_config_ =
        std::make_unique<Filters::Common::Fault::FaultAbortConfig>(fault.abort());
  }

  if (fault.has_delay()) {
    request_delay_config_ =
        std::make_unique<Filters::Common::Fault::FaultDelayConfig>(fault.delay());
  }

  upstream_cluster_ = fault.upstream_cluster();

  for (const auto& node : fault.downstream_nodes()) {
    downstream_nodes_.insert(node);
  }

  if (fault.has_max_active_faults()) {
    max_active_faults_ = fault.max_active_faults().value();
  }

  if (fault.has_response_rate_limit()) {
    response_rate_limit_ =
        std::make_unique<Filters::Common::Fault::FaultRateLimitConfig>(fault.response_rate_limit());
  }
}

FaultFilterConfig::FaultFilterConfig(
    const envoy::extensions::filters::http::fault::v3::HTTPFault& fault, Runtime::Loader& runtime,
    const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source)
    : settings_(fault), runtime_(runtime), stats_(generateStats(stats_prefix, scope)),
      scope_(scope), time_source_(time_source),
      stat_name_set_(scope.symbolTable().makeSet("Fault")),
      aborts_injected_(stat_name_set_->add("aborts_injected")),
      delays_injected_(stat_name_set_->add("delays_injected")),
      stats_prefix_(stat_name_set_->add(absl::StrCat(stats_prefix, "fault"))) {}

void FaultFilterConfig::incCounter(Stats::StatName downstream_cluster, Stats::StatName stat_name) {
  if (!settings_.disableDownstreamClusterStats()) {
    Stats::Utility::counterFromStatNames(scope_, {stats_prefix_, downstream_cluster, stat_name})
        .inc();
  }
}

FaultFilter::FaultFilter(FaultFilterConfigSharedPtr config) : config_(config) {}

FaultFilter::~FaultFilter() {
  ASSERT(delay_timer_ == nullptr);
  ASSERT(response_limiter_ == nullptr || response_limiter_->destroyed());
}

// Delays and aborts are independent events. One can inject a delay
// followed by an abort or inject just a delay or abort. In this callback,
// if we inject a delay, then we will inject the abort in the delay timer
// callback.
Http::FilterHeadersStatus FaultFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Route-level configuration overrides filter-level configuration
  // NOTE: We should not use runtime when reading from route-level
  // faults. In other words, runtime is supported only when faults are
  // configured at the filter level.
  fault_settings_ = config_->settings();
  const auto* per_route_settings =
      Http::Utility::resolveMostSpecificPerFilterConfig<FaultSettings>(decoder_callbacks_);
  fault_settings_ = per_route_settings ? per_route_settings : fault_settings_;

  if (!matchesTargetUpstreamCluster()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!matchesDownstreamNodes(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Check for header matches
  if (!Http::HeaderUtility::matchHeaders(headers, fault_settings_->filterHeaders())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (headers.EnvoyDownstreamServiceCluster()) {
    downstream_cluster_ = std::string(headers.getEnvoyDownstreamServiceClusterValue());
    if (!downstream_cluster_.empty() && !fault_settings_->disableDownstreamClusterStats()) {
      downstream_cluster_storage_ = std::make_unique<Stats::StatNameDynamicStorage>(
          downstream_cluster_, config_->scope().symbolTable());
    }

    downstream_cluster_delay_percent_key_ =
        fmt::format("fault.http.{}.delay.fixed_delay_percent", downstream_cluster_);
    downstream_cluster_abort_percent_key_ =
        fmt::format("fault.http.{}.abort.abort_percent", downstream_cluster_);
    downstream_cluster_delay_duration_key_ =
        fmt::format("fault.http.{}.delay.fixed_duration_ms", downstream_cluster_);
    downstream_cluster_abort_http_status_key_ =
        fmt::format("fault.http.{}.abort.http_status", downstream_cluster_);
    downstream_cluster_abort_grpc_status_key_ =
        fmt::format("fault.http.{}.abort.grpc_status", downstream_cluster_);
  }

  maybeSetupResponseRateLimit(headers);

  if (maybeSetupDelay(headers)) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (maybeDoAbort(headers)) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

bool FaultFilter::maybeSetupDelay(const Http::RequestHeaderMap& request_headers) {
  absl::optional<std::chrono::milliseconds> duration = delayDuration(request_headers);
  if (duration.has_value() && tryIncActiveFaults()) {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this, &request_headers]() -> void { postDelayInjection(request_headers); });
    ENVOY_LOG(debug, "fault: delaying request {}ms", duration.value().count());
    delay_timer_->enableTimer(duration.value(), &decoder_callbacks_->scope());
    recordDelaysInjectedStats();
    decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DelayInjected);
    return true;
  }
  return false;
}

bool FaultFilter::maybeDoAbort(const Http::RequestHeaderMap& request_headers) {
  absl::optional<Http::Code> http_status;
  absl::optional<Grpc::Status::GrpcStatus> grpc_status;
  std::tie(http_status, grpc_status) = abortStatus(request_headers);

  if (http_status.has_value() && tryIncActiveFaults()) {
    abortWithStatus(http_status.value(), grpc_status);
    return true;
  }

  return false;
}

void FaultFilter::maybeSetupResponseRateLimit(const Http::RequestHeaderMap& request_headers) {
  if (!isResponseRateLimitEnabled(request_headers)) {
    return;
  }

  absl::optional<uint64_t> rate_kbps =
      fault_settings_->responseRateLimit()->rateKbps(&request_headers);
  if (!rate_kbps.has_value()) {
    return;
  }

  if (!tryIncActiveFaults()) {
    return;
  }

  config_->stats().response_rl_injected_.inc();

  response_limiter_ = std::make_unique<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter>(
      rate_kbps.value(), encoder_callbacks_->encoderBufferLimit(),
      [this] { encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark(); },
      [this] { encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark(); },
      [this](Buffer::Instance& data, bool end_stream) {
        encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
      },
      [this] { encoder_callbacks_->continueEncoding(); },
      [](uint64_t, bool) {
        // write stats callback.
      },
      config_->timeSource(), decoder_callbacks_->dispatcher(), decoder_callbacks_->scope());
}

bool FaultFilter::faultOverflow() {
  const uint64_t max_faults = config_->runtime().snapshot().getInteger(
      fault_settings_->maxActiveFaultsRuntime(), fault_settings_->maxActiveFaults().has_value()
                                                     ? fault_settings_->maxActiveFaults().value()
                                                     : std::numeric_limits<uint64_t>::max());
  // Note: Since we don't compare/swap here this is a fuzzy limit which is similar to how the
  // other circuit breakers work.
  if (config_->stats().active_faults_.value() >= max_faults) {
    config_->stats().faults_overflow_.inc();
    return true;
  }

  return false;
}

bool FaultFilter::isDelayEnabled(const Http::RequestHeaderMap& request_headers) {
  const auto request_delay = fault_settings_->requestDelay();
  if (request_delay == nullptr) {
    return false;
  }

  if (!downstream_cluster_delay_percent_key_.empty()) {
    return config_->runtime().snapshot().featureEnabled(
        downstream_cluster_delay_percent_key_, request_delay->percentage(&request_headers));
  }
  return config_->runtime().snapshot().featureEnabled(fault_settings_->delayPercentRuntime(),
                                                      request_delay->percentage(&request_headers));
}

bool FaultFilter::isAbortEnabled(const Http::RequestHeaderMap& request_headers) {
  const auto request_abort = fault_settings_->requestAbort();
  if (request_abort == nullptr) {
    return false;
  }

  if (!downstream_cluster_abort_percent_key_.empty()) {
    return config_->runtime().snapshot().featureEnabled(
        downstream_cluster_abort_percent_key_, request_abort->percentage(&request_headers));
  }
  return config_->runtime().snapshot().featureEnabled(fault_settings_->abortPercentRuntime(),
                                                      request_abort->percentage(&request_headers));
}

bool FaultFilter::isResponseRateLimitEnabled(const Http::RequestHeaderMap& request_headers) {
  if (!isResponseRateLimitConfigured()) {
    return false;
  }

  // TODO(mattklein123): Allow runtime override via downstream cluster similar to the other keys.
  return config_->runtime().snapshot().featureEnabled(
      fault_settings_->responseRateLimitPercentRuntime(),
      fault_settings_->responseRateLimit()->percentage(&request_headers));
}

absl::optional<std::chrono::milliseconds>
FaultFilter::delayDuration(const Http::RequestHeaderMap& request_headers) {
  absl::optional<std::chrono::milliseconds> ret;

  if (!isDelayEnabled(request_headers)) {
    return ret;
  }

  // See if the configured delay provider has a default delay, if not there is no delay (e.g.,
  // header configuration and no/invalid header).
  auto config_duration = fault_settings_->requestDelay()->duration(&request_headers);
  if (!config_duration.has_value()) {
    return ret;
  }

  std::chrono::milliseconds duration =
      std::chrono::milliseconds(config_->runtime().snapshot().getInteger(
          fault_settings_->delayDurationRuntime(), config_duration.value().count()));
  if (!downstream_cluster_delay_duration_key_.empty()) {
    duration = std::chrono::milliseconds(config_->runtime().snapshot().getInteger(
        downstream_cluster_delay_duration_key_, duration.count()));
  }

  // Delay only if the duration is >0ms
  if (duration.count() > 0) {
    ret = duration;
  }

  return ret;
}

AbortHttpAndGrpcStatus FaultFilter::abortStatus(const Http::RequestHeaderMap& request_headers) {
  if (!isAbortEnabled(request_headers)) {
    return AbortHttpAndGrpcStatus{absl::nullopt, absl::nullopt};
  }

  auto http_status = abortHttpStatus(request_headers);
  // If http status code is set, then gRPC status won't be used.
  if (http_status.has_value()) {
    return AbortHttpAndGrpcStatus{http_status, absl::nullopt};
  }

  auto grpc_status = abortGrpcStatus(request_headers);
  // If gRPC status code is set, then http status will be set to Http::Code::OK (200)
  if (grpc_status.has_value()) {
    return AbortHttpAndGrpcStatus{Http::Code::OK, grpc_status};
  }

  return AbortHttpAndGrpcStatus{absl::nullopt, absl::nullopt};
}

absl::optional<Http::Code>
FaultFilter::abortHttpStatus(const Http::RequestHeaderMap& request_headers) {
  // See if the configured abort provider has a default status code, if not there is no abort status
  // code (e.g., header configuration and no/invalid header).
  auto http_status = fault_settings_->requestAbort()->httpStatusCode(&request_headers);
  if (!http_status.has_value()) {
    return absl::nullopt;
  }

  auto default_http_status_code = static_cast<uint64_t>(http_status.value());
  auto runtime_http_status_code = config_->runtime().snapshot().getInteger(
      fault_settings_->abortHttpStatusRuntime(), default_http_status_code);

  if (!downstream_cluster_abort_http_status_key_.empty()) {
    runtime_http_status_code = config_->runtime().snapshot().getInteger(
        downstream_cluster_abort_http_status_key_, default_http_status_code);
  }

  return static_cast<Http::Code>(runtime_http_status_code);
}

absl::optional<Grpc::Status::GrpcStatus>
FaultFilter::abortGrpcStatus(const Http::RequestHeaderMap& request_headers) {
  auto grpc_status = fault_settings_->requestAbort()->grpcStatusCode(&request_headers);
  if (!grpc_status.has_value()) {
    return absl::nullopt;
  }

  auto default_grpc_status_code = static_cast<uint64_t>(grpc_status.value());
  auto runtime_grpc_status_code = config_->runtime().snapshot().getInteger(
      fault_settings_->abortGrpcStatusRuntime(), default_grpc_status_code);

  if (!downstream_cluster_abort_grpc_status_key_.empty()) {
    runtime_grpc_status_code = config_->runtime().snapshot().getInteger(
        downstream_cluster_abort_grpc_status_key_, default_grpc_status_code);
  }

  return static_cast<Grpc::Status::GrpcStatus>(runtime_grpc_status_code);
}

void FaultFilter::recordDelaysInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty() && !fault_settings_->disableDownstreamClusterStats()) {
    config_->incDelays(downstream_cluster_storage_->statName());
  }

  config_->stats().delays_injected_.inc();
}

void FaultFilter::recordAbortsInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty() && !fault_settings_->disableDownstreamClusterStats()) {
    config_->incAborts(downstream_cluster_storage_->statName());
  }

  config_->stats().aborts_injected_.inc();
}

Http::FilterDataStatus FaultFilter::decodeData(Buffer::Instance&, bool) {
  if (delay_timer_ == nullptr) {
    return Http::FilterDataStatus::Continue;
  }
  // If the request is too large, stop reading new data until the buffer drains.
  return Http::FilterDataStatus::StopIterationAndWatermark;
}

Http::FilterTrailersStatus FaultFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return delay_timer_ == nullptr ? Http::FilterTrailersStatus::Continue
                                 : Http::FilterTrailersStatus::StopIteration;
}

FaultFilterStats FaultFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "fault.";
  return {ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                 POOL_GAUGE_PREFIX(scope, final_prefix))};
}

bool FaultFilter::tryIncActiveFaults() {
  // Only charge 1 active fault per filter in case we are injecting multiple faults.
  // Since we count at most one active fault per filter, we also allow multiple faults
  // per filter without checking for overflow.
  if (fault_active_) {
    return true;
  }

  // We only check for overflow when attempting to perform a fault. Note that this means that a
  // single request might increment the counter more than once if it tries to apply multiple faults,
  // and it is also possible for it to fail the first check then succeed on the second (should
  // another thread decrement the active fault gauge).
  if (faultOverflow()) {
    return false;
  }

  // TODO(mattklein123): Consider per-fault type active fault gauges.
  config_->stats().active_faults_.inc();
  fault_active_ = true;

  return true;
}

void FaultFilter::onDestroy() {
  resetTimerState();
  if (response_limiter_ != nullptr) {
    response_limiter_->destroy();
  }
  if (fault_active_) {
    config_->stats().active_faults_.dec();
  }
}

bool FaultFilter::isResponseRateLimitConfigured() {
  return fault_settings_->responseRateLimit() != nullptr;
}

void FaultFilter::postDelayInjection(const Http::RequestHeaderMap& request_headers) {
  resetTimerState();

  // Delays can be followed by aborts
  absl::optional<Http::Code> http_status;
  absl::optional<Grpc::Status::GrpcStatus> grpc_status;
  std::tie(http_status, grpc_status) = abortStatus(request_headers);

  if (http_status.has_value()) {
    abortWithStatus(http_status.value(), grpc_status);
  } else {
    // Should not continue to count as an active fault after the delay has elapsed if no other type
    // of fault is active. As the delay timer is always done at this point and followed abort faults
    // have been checked earlier, here we just check if there's a response rate limit configured.
    ASSERT(fault_active_);
    ASSERT(delay_timer_ == nullptr);
    if (!isResponseRateLimitConfigured()) {
      config_->stats().active_faults_.dec();
      fault_active_ = false;
    }

    // Continue request processing.
    decoder_callbacks_->continueDecoding();
  }
}

void FaultFilter::abortWithStatus(Http::Code http_status_code,
                                  absl::optional<Grpc::Status::GrpcStatus> grpc_status) {
  recordAbortsInjectedStats();
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FaultInjected);
  decoder_callbacks_->sendLocalReply(http_status_code, "fault filter abort", nullptr, grpc_status,
                                     RcDetails::get().FaultAbort);
}

bool FaultFilter::matchesTargetUpstreamCluster() {
  bool matches = true;

  if (!fault_settings_->upstreamCluster().empty()) {
    Router::RouteConstSharedPtr route = decoder_callbacks_->route();
    matches = route && route->routeEntry() &&
              (route->routeEntry()->clusterName() == fault_settings_->upstreamCluster());
  }

  return matches;
}

bool FaultFilter::matchesDownstreamNodes(const Http::RequestHeaderMap& headers) {
  if (fault_settings_->downstreamNodes().empty()) {
    return true;
  }

  if (!headers.EnvoyDownstreamServiceNode()) {
    return false;
  }

  const absl::string_view downstream_node = headers.getEnvoyDownstreamServiceNodeValue();
  return fault_settings_->downstreamNodes().find(downstream_node) !=
         fault_settings_->downstreamNodes().end();
}

void FaultFilter::resetTimerState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

Http::FilterDataStatus FaultFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_limiter_ != nullptr) {
    response_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus FaultFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (response_limiter_ != nullptr) {
    return response_limiter_->onTrailers() ? Http::FilterTrailersStatus::StopIteration
                                           : Http::FilterTrailersStatus::Continue;
  }

  return Http::FilterTrailersStatus::Continue;
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
