#include "extensions/filters/http/fault/fault_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

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
      max_active_faults_runtime_(PROTOBUF_GET_STRING_OR_DEFAULT(
          fault, max_active_faults_runtime, RuntimeKeys::get().MaxActiveFaultsKey)),
      response_rate_limit_percent_runtime_(
          PROTOBUF_GET_STRING_OR_DEFAULT(fault, response_rate_limit_percent_runtime,
                                         RuntimeKeys::get().ResponseRateLimitPercentKey)) {
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
  Stats::SymbolTable::StoragePtr storage =
      scope_.symbolTable().join({stats_prefix_, downstream_cluster, stat_name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
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
  if (decoder_callbacks_->route() && decoder_callbacks_->route()->routeEntry()) {
    const std::string& name = Extensions::HttpFilters::HttpFilterNames::get().Fault;
    const auto* route_entry = decoder_callbacks_->route()->routeEntry();

    const auto* per_route_settings =
        route_entry->mostSpecificPerFilterConfigTyped<FaultSettings>(name);
    fault_settings_ = per_route_settings ? per_route_settings : fault_settings_;
  }

  if (faultOverflow()) {
    return Http::FilterHeadersStatus::Continue;
  }

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
    downstream_cluster_ =
        std::string(headers.EnvoyDownstreamServiceCluster()->value().getStringView());
    if (!downstream_cluster_.empty()) {
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
  }

  maybeSetupResponseRateLimit(headers);

  absl::optional<std::chrono::milliseconds> duration = delayDuration(headers);
  if (duration.has_value()) {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this, &headers]() -> void { postDelayInjection(headers); });
    ENVOY_LOG(debug, "fault: delaying request {}ms", duration.value().count());
    delay_timer_->enableTimer(duration.value(), &decoder_callbacks_->scope());
    recordDelaysInjectedStats();
    decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DelayInjected);
    return Http::FilterHeadersStatus::StopIteration;
  }

  const auto abort_code = abortHttpStatus(headers);
  if (abort_code.has_value()) {
    abortWithHTTPStatus(abort_code.value());
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

void FaultFilter::maybeSetupResponseRateLimit(const Http::RequestHeaderMap& request_headers) {
  if (fault_settings_->responseRateLimit() == nullptr) {
    return;
  }

  absl::optional<uint64_t> rate_kbps = fault_settings_->responseRateLimit()->rateKbps(
      request_headers.get(Filters::Common::Fault::HeaderNames::get().ThroughputResponse));
  if (!rate_kbps.has_value()) {
    return;
  }

  // TODO(mattklein123): Allow runtime override via downstream cluster similar to the other keys.
  if (!config_->runtime().snapshot().featureEnabled(
          fault_settings_->responseRateLimitPercentRuntime(),
          fault_settings_->responseRateLimit()->percentage())) {
    return;
  }

  // General stats. All injected faults are considered a single aggregate active fault.
  maybeIncActiveFaults();
  config_->stats().response_rl_injected_.inc();

  response_limiter_ = std::make_unique<StreamRateLimiter>(
      rate_kbps.value(), encoder_callbacks_->encoderBufferLimit(),
      [this] { encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark(); },
      [this] { encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark(); },
      [this](Buffer::Instance& data, bool end_stream) {
        encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
      },
      [this] { encoder_callbacks_->continueEncoding(); }, config_->timeSource(),
      decoder_callbacks_->dispatcher(), decoder_callbacks_->scope());
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

bool FaultFilter::isDelayEnabled() {
  const auto request_delay = fault_settings_->requestDelay();
  if (request_delay == nullptr) {
    return false;
  }

  if (!downstream_cluster_delay_percent_key_.empty()) {
    return config_->runtime().snapshot().featureEnabled(downstream_cluster_delay_percent_key_,
                                                        request_delay->percentage());
  }
  return config_->runtime().snapshot().featureEnabled(fault_settings_->delayPercentRuntime(),
                                                      request_delay->percentage());
}

bool FaultFilter::isAbortEnabled() {
  const auto request_abort = fault_settings_->requestAbort();
  if (request_abort == nullptr) {
    return false;
  }

  if (!downstream_cluster_abort_percent_key_.empty()) {
    return config_->runtime().snapshot().featureEnabled(downstream_cluster_abort_percent_key_,
                                                        request_abort->percentage());
  }
  return config_->runtime().snapshot().featureEnabled(fault_settings_->abortPercentRuntime(),
                                                      request_abort->percentage());
}

absl::optional<std::chrono::milliseconds>
FaultFilter::delayDuration(const Http::RequestHeaderMap& request_headers) {
  absl::optional<std::chrono::milliseconds> ret;

  if (!isDelayEnabled()) {
    return ret;
  }

  // See if the configured delay provider has a default delay, if not there is no delay (e.g.,
  // header configuration and no/invalid header).
  auto config_duration = fault_settings_->requestDelay()->duration(
      request_headers.get(Filters::Common::Fault::HeaderNames::get().DelayRequest));
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

absl::optional<Http::Code>
FaultFilter::abortHttpStatus(const Http::RequestHeaderMap& request_headers) {
  if (!isAbortEnabled()) {
    return absl::nullopt;
  }

  // See if the configured abort provider has a default status code, if not there is no abort status
  // code (e.g., header configuration and no/invalid header).
  const auto config_abort = fault_settings_->requestAbort()->statusCode(
      request_headers.get(Filters::Common::Fault::HeaderNames::get().AbortRequest));
  if (!config_abort.has_value()) {
    return absl::nullopt;
  }

  auto status_code = static_cast<uint64_t>(config_abort.value());
  auto code = static_cast<Http::Code>(config_->runtime().snapshot().getInteger(
      fault_settings_->abortHttpStatusRuntime(), status_code));

  if (!downstream_cluster_abort_http_status_key_.empty()) {
    code = static_cast<Http::Code>(config_->runtime().snapshot().getInteger(
        downstream_cluster_abort_http_status_key_, status_code));
  }

  return code;
}

void FaultFilter::recordDelaysInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty()) {
    config_->incDelays(downstream_cluster_storage_->statName());
  }

  // General stats. All injected faults are considered a single aggregate active fault.
  maybeIncActiveFaults();
  config_->stats().delays_injected_.inc();
}

void FaultFilter::recordAbortsInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty()) {
    config_->incAborts(downstream_cluster_storage_->statName());
  }

  // General stats. All injected faults are considered a single aggregate active fault.
  maybeIncActiveFaults();
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

void FaultFilter::maybeIncActiveFaults() {
  // Only charge 1 active fault per filter in case we are injecting multiple faults.
  if (fault_active_) {
    return;
  }

  // TODO(mattklein123): Consider per-fault type active fault gauges.
  config_->stats().active_faults_.inc();
  fault_active_ = true;
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

void FaultFilter::postDelayInjection(const Http::RequestHeaderMap& request_headers) {
  resetTimerState();

  // Delays can be followed by aborts
  const auto abort_code = abortHttpStatus(request_headers);
  if (abort_code.has_value()) {
    abortWithHTTPStatus(abort_code.value());
  } else {
    // Continue request processing.
    decoder_callbacks_->continueDecoding();
  }
}

void FaultFilter::abortWithHTTPStatus(Http::Code abort_code) {
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FaultInjected);
  decoder_callbacks_->sendLocalReply(abort_code, "fault filter abort", nullptr, absl::nullopt,
                                     RcDetails::get().FaultAbort);
  recordAbortsInjectedStats();
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

  const absl::string_view downstream_node =
      headers.EnvoyDownstreamServiceNode()->value().getStringView();
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
    return response_limiter_->onTrailers();
  }

  return Http::FilterTrailersStatus::Continue;
}

StreamRateLimiter::StreamRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                                     std::function<void()> pause_data_cb,
                                     std::function<void()> resume_data_cb,
                                     std::function<void(Buffer::Instance&, bool)> write_data_cb,
                                     std::function<void()> continue_cb, TimeSource& time_source,
                                     Event::Dispatcher& dispatcher, const ScopeTrackedObject& scope)
    : // bytes_per_time_slice is KiB converted to bytes divided by the number of ticks per second.
      bytes_per_time_slice_((max_kbps * 1024) / SecondDivisor), write_data_cb_(write_data_cb),
      continue_cb_(continue_cb), scope_(scope),
      // The token bucket is configured with a max token count of the number of ticks per second,
      // and refills at the same rate, so that we have a per second limit which refills gradually in
      // ~63ms intervals.
      token_bucket_(SecondDivisor, time_source, SecondDivisor),
      token_timer_(dispatcher.createTimer([this] { onTokenTimer(); })),
      buffer_(resume_data_cb, pause_data_cb) {
  ASSERT(bytes_per_time_slice_ > 0);
  ASSERT(max_buffered_data > 0);
  buffer_.setWatermarks(max_buffered_data);
}

void StreamRateLimiter::onTokenTimer() {
  ENVOY_LOG(trace, "limiter: timer wakeup: buffered={}", buffer_.length());
  Buffer::OwnedImpl data_to_write;

  if (!saw_data_) {
    // The first time we see any data on this stream (via writeData()), reset the number of tokens
    // to 1. This will ensure that we start pacing the data at the desired rate (and don't send a
    // full 1s of data right away which might not introduce enough delay for a stream that doesn't
    // have enough data to span more than 1s of rate allowance). Once we reset, we will subsequently
    // allow for bursting within the second to account for our data provider being bursty.
    token_bucket_.reset(1);
    saw_data_ = true;
  }

  // Compute the number of tokens needed (rounded up), try to obtain that many tickets, and then
  // figure out how many bytes to write given the number of tokens we actually got.
  const uint64_t tokens_needed =
      (buffer_.length() + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = token_bucket_.consume(tokens_needed, true);
  const uint64_t bytes_to_write =
      std::min(tokens_obtained * bytes_per_time_slice_, buffer_.length());
  ENVOY_LOG(trace, "limiter: tokens_needed={} tokens_obtained={} to_write={}", tokens_needed,
            tokens_obtained, bytes_to_write);

  // Move the data to write into the output buffer with as little copying as possible.
  // NOTE: This might be moving zero bytes, but that should work fine.
  data_to_write.move(buffer_, bytes_to_write);

  // If the buffer still contains data in it, we couldn't get enough tokens, so schedule the next
  // token available time.
  if (buffer_.length() > 0) {
    const std::chrono::milliseconds ms = token_bucket_.nextTokenAvailable();
    if (ms.count() > 0) {
      ENVOY_LOG(trace, "limiter: scheduling wakeup for {}ms", ms.count());
      token_timer_->enableTimer(ms, &scope_);
    }
  }

  // Write the data out, indicating end stream if we saw end stream, there is no further data to
  // send, and there are no trailers.
  write_data_cb_(data_to_write, saw_end_stream_ && buffer_.length() == 0 && !saw_trailers_);

  // If there is no more data to send and we saw trailers, we need to continue iteration to release
  // the trailers to further filters.
  if (buffer_.length() == 0 && saw_trailers_) {
    continue_cb_();
  }
}

void StreamRateLimiter::writeData(Buffer::Instance& incoming_buffer, bool end_stream) {
  ENVOY_LOG(trace, "limiter: incoming data length={} buffered={}", incoming_buffer.length(),
            buffer_.length());
  buffer_.move(incoming_buffer);
  saw_end_stream_ = end_stream;
  if (!token_timer_->enabled()) {
    // TODO(mattklein123): In an optimal world we would be able to continue iteration with the data
    // we want in the buffer, but have a way to clear end_stream in case we can't send it all.
    // The filter API does not currently support that and it will not be a trivial change to add.
    // Instead we cheat here by scheduling the token timer to run immediately after the stack is
    // unwound, at which point we can directly called encode/decodeData.
    token_timer_->enableTimer(std::chrono::milliseconds(0), &scope_);
  }
}

Http::FilterTrailersStatus StreamRateLimiter::onTrailers() {
  saw_end_stream_ = true;
  saw_trailers_ = true;
  return buffer_.length() > 0 ? Http::FilterTrailersStatus::StopIteration
                              : Http::FilterTrailersStatus::Continue;
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
