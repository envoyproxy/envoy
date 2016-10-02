#include "config_impl.h"
#include "retry_state_impl.h"
#include "router.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/pooled_stream_encoder.h"
#include "common/http/utility.h"

namespace Router {

bool FilterUtility::shouldShadow(const ShadowPolicy& policy, Runtime::Loader& runtime,
                                 uint64_t stable_random) {
  if (policy.cluster().empty()) {
    return false;
  }

  if (!policy.runtimeKey().empty() &&
      !runtime.snapshot().featureEnabled(policy.runtimeKey(), 0, stable_random, 10000UL)) {
    return false;
  }

  return true;
}

FilterUtility::TimeoutData FilterUtility::finalTimeout(const RouteEntry& route,
                                                       Http::HeaderMap& request_headers) {
  // See if there is a user supplied timeout in a request header. If there is we take that,
  // otherwise we use the default.
  TimeoutData timeout;
  timeout.global_timeout_ = route.timeout();
  const std::string& header_timeout_string =
      request_headers.get(Http::Headers::get().EnvoyUpstreamRequestTimeoutMs);
  uint64_t header_timeout;
  if (!header_timeout_string.empty()) {
    if (StringUtil::atoul(header_timeout_string.c_str(), header_timeout)) {
      timeout.global_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.remove(Http::Headers::get().EnvoyUpstreamRequestTimeoutMs);
  }

  // See if there is a per try/retry timeout. If it's >= global we just ignore it.
  const std::string& per_try_timeout_string =
      request_headers.get(Http::Headers::get().EnvoyUpstreamRequestPerTryTimeoutMs);
  if (!per_try_timeout_string.empty()) {
    if (StringUtil::atoul(per_try_timeout_string.c_str(), header_timeout)) {
      timeout.per_try_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.remove(Http::Headers::get().EnvoyUpstreamRequestPerTryTimeoutMs);
  }

  if (timeout.per_try_timeout_ >= timeout.global_timeout_) {
    timeout.per_try_timeout_ = std::chrono::milliseconds(0);
  }

  // See if there is any timeout to write in the expected timeout header.
  uint64_t expected_timeout = timeout.per_try_timeout_.count();
  if (expected_timeout == 0) {
    expected_timeout = timeout.global_timeout_.count();
  }

  if (expected_timeout > 0) {
    request_headers.replaceViaCopy(Http::Headers::get().EnvoyExpectedRequestTimeoutMs,
                                   std::to_string(expected_timeout));
  }

  return timeout;
}

Filter::Filter(FilterConfig& config) : config_(config) {}

Filter::~Filter() {
  // Upstream resources should already have been cleaned.
  ASSERT(!upstream_request_);
  ASSERT(!retry_state_);
}

const std::string& Filter::upstreamZone() {
  return upstream_host_ ? upstream_host_->zone() : EMPTY_STRING;
}

void Filter::chargeUpstreamCode(const Http::HeaderMap& response_headers) {
  if (config_.emit_dynamic_stats_ && !callbacks_->requestInfo().healthCheck()) {
    bool is_canary = (response_headers.get(Http::Headers::get().EnvoyUpstreamCanary) == "true") ||
                     (upstream_host_ ? upstream_host_->canary() : false);

    if (upstream_host_) {
      upstream_host_->outlierDetector().putHttpResponseCode(
          Http::Utility::getResponseStatus(response_headers));
    }

    Http::CodeUtility::ResponseStatInfo info{
        config_.stats_store_, stat_prefix_, response_headers,
        downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true",
        route_->virtualHostName(), request_vcluster_ ? request_vcluster_->name() : "",
        config_.service_zone_, upstreamZone(), is_canary};

    Http::CodeUtility::chargeResponseStat(info);

    for (const std::string& alt_prefix : alt_stat_prefixes_) {
      Http::CodeUtility::ResponseStatInfo info{
          config_.stats_store_, alt_prefix, response_headers,
          downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true", "", "",
          config_.service_zone_, upstreamZone(), is_canary};

      Http::CodeUtility::chargeResponseStat(info);
    }
  }
}

void Filter::chargeUpstreamCode(Http::Code code) {
  Http::HeaderMapImpl fake_response_headers{
      {Http::Headers::get().Status, std::to_string(enumToInt(code))}};
  chargeUpstreamCode(fake_response_headers);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  downstream_headers_ = &headers;

  // Only increment rq total stat if we actually decode headers here. This does not count requests
  // that get handled by earlier filters.
  config_.stats_.rq_total_.inc();

  // First determine if we need to do a redirect before we do anything else.
  const RedirectEntry* redirect = callbacks_->routeTable().redirectRequest(headers);
  if (redirect) {
    config_.stats_.rq_redirect_.inc();
    Http::Utility::sendRedirect(*callbacks_, redirect->newPath(headers));
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Determine if there is a route match.
  route_ = callbacks_->routeTable().routeForRequest(headers);
  if (!route_) {
    config_.stats_.no_route_.inc();
    stream_log_debug("no cluster match for URL '{}'", *callbacks_,
                     headers.get(Http::Headers::get().Path));

    callbacks_->requestInfo().onFailedResponse(Http::AccessLog::FailureReason::NoRouteFound);
    Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::NotFound))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Set up stat prefixes, etc.
  request_vcluster_ = route_->virtualCluster(headers);
  stat_prefix_ = fmt::format("cluster.{}.", route_->clusterName());
  stream_log_debug("cluster '{}' match for URL '{}'", *callbacks_, route_->clusterName(),
                   headers.get(Http::Headers::get().Path));

  const Upstream::Cluster& cluster = *config_.cm_.get(route_->clusterName());
  const std::string& cluster_alt_name = cluster.altStatName();
  if (!cluster_alt_name.empty()) {
    alt_stat_prefixes_.push_back(fmt::format("cluster.{}.", cluster_alt_name));
  }

  const std::string& request_alt_name = headers.get(Http::Headers::get().EnvoyUpstreamAltStatName);
  if (!request_alt_name.empty()) {
    alt_stat_prefixes_.push_back(
        fmt::format("cluster.{}.{}.", route_->clusterName(), request_alt_name));
  }
  headers.remove(Http::Headers::get().EnvoyUpstreamAltStatName);

  // See if we are supposed to immediately kill some percentage of this cluster's traffic.
  if (config_.runtime_.snapshot().featureEnabled(
          fmt::format("upstream.maintenance_mode.{}", route_->clusterName()), 0)) {
    callbacks_->requestInfo().onFailedResponse(Http::AccessLog::FailureReason::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable);
    Http::Utility::sendLocalReply(*callbacks_, Http::Code::ServiceUnavailable, "maintenance mode");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Fetch a connection pool for the upstream cluster.
  Http::ConnectionPool::Instance* conn_pool =
      config_.cm_.httpConnPoolForCluster(route_->clusterName(), finalPriority());
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  timeout_ = FilterUtility::finalTimeout(*route_, headers);
  route_->finalizeRequestHeaders(headers);
  retry_state_ = createRetryState(route_->retryPolicy(), headers, cluster, config_.runtime_,
                                  config_.random_, callbacks_->dispatcher(), finalPriority());
  do_shadowing_ =
      FilterUtility::shouldShadow(route_->shadowPolicy(), config_.runtime_, callbacks_->streamId());

#ifndef NDEBUG
  headers.iterate([this](const Http::LowerCaseString& key, const std::string& value)
                      -> void { stream_log_debug("  '{}':'{}'", *callbacks_, key.get(), value); });
#endif

  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool));
  upstream_request_->upstream_encoder_->encodeHeaders(headers, end_stream);
  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

void Filter::sendNoHealthyUpstreamResponse() {
  callbacks_->requestInfo().onFailedResponse(Http::AccessLog::FailureReason::NoHealthyUpstream);
  chargeUpstreamCode(Http::Code::ServiceUnavailable);
  Http::Utility::sendLocalReply(*callbacks_, Http::Code::ServiceUnavailable, "no healthy upstream");
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  upstream_request_->upstream_encoder_->encodeData(data, end_stream);
  if (end_stream) {
    onRequestComplete();
  }

  // If we are potentially going to retry or shadow this request we need to buffer.
  return (retry_state_ && retry_state_->enabled()) || do_shadowing_
             ? Http::FilterDataStatus::StopIterationAndBuffer
             : Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
  downstream_trailers_ = &trailers;
  upstream_request_->upstream_encoder_->encodeTrailers(trailers);
  onRequestComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

void Filter::cleanup() {
  upstream_request_.reset();
  retry_state_.reset();
  if (response_timeout_) {
    response_timeout_->disableTimer();
    response_timeout_.reset();
  }
}

Upstream::ResourcePriority Filter::finalPriority() {
  // Virtual cluster priority trumps route priority if the route has a virtual cluster.
  if (request_vcluster_) {
    return request_vcluster_->priority();
  } else {
    return route_->priority();
  }
}

void Filter::maybeDoShadowing() {
  if (!do_shadowing_) {
    return;
  }

  ASSERT(!route_->shadowPolicy().cluster().empty());
  Http::MessagePtr request(new Http::RequestMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_headers_)}));
  if (callbacks_->decodingBuffer()) {
    request->body(Buffer::InstancePtr{new Buffer::OwnedImpl(*callbacks_->decodingBuffer())});
  }
  if (downstream_trailers_) {
    request->trailers(Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_trailers_)});
  }

  config_.shadowWriter().shadow(route_->shadowPolicy().cluster(), std::move(request),
                                timeout_.global_timeout_);
}

void Filter::onRequestComplete() {
  downstream_end_stream_ = true;

  // Possible that we got an immediate reset.
  if (upstream_request_) {
    // Even if we got an immediate reset, we could still shadow, but that is a riskier change and
    // seems unnecessary right now.
    maybeDoShadowing();

    upstream_request_->setupPerTryTimeout();
    if (timeout_.global_timeout_.count() > 0) {
      response_timeout_ =
          callbacks_->dispatcher().createTimer([this]() -> void { onResponseTimeout(); });
      response_timeout_->enableTimer(timeout_.global_timeout_);
    }
  }
}

void Filter::onResetStream() {
  if (upstream_request_) {
    upstream_request_->upstream_encoder_->resetStream();
  }

  cleanup();
}

void Filter::onResponseTimeout() {
  stream_log_debug("upstream timeout", *callbacks_);
  config_.cm_.get(route_->clusterName())->stats().upstream_rq_timeout_.inc();

  // It's possible to timeout during a retry backoff delay when we have no upstream request. In
  // this case we fake a reset since onUpstreamReset() doesn't care.
  if (upstream_request_) {
    upstream_request_->upstream_encoder_->resetStream();
  }

  onUpstreamReset(UpstreamResetType::GlobalTimeout, Optional<Http::StreamResetReason>());
}

void Filter::onUpstreamReset(UpstreamResetType type,
                             const Optional<Http::StreamResetReason>& reset_reason) {
  ASSERT(type == UpstreamResetType::GlobalTimeout || upstream_request_);
  if (type == UpstreamResetType::Reset) {
    stream_log_debug("upstream reset", *callbacks_);
  }

  // We don't retry on a global timeout or if we already started the response.
  if (type != UpstreamResetType::GlobalTimeout && !downstream_response_started_ && retry_state_ &&
      retry_state_->shouldRetry(nullptr, reset_reason, [this]() -> void { doRetry(); }) &&
      setupRetry(true)) {
    return;
  }

  // This will destroy any created retry timers.
  cleanup();

  // If we have never sent any response, send a 503. Otherwise just reset the ongoing response.
  if (downstream_response_started_) {
    callbacks_->resetStream();
  } else {
    Http::Code code;
    const char* body;
    if (type == UpstreamResetType::GlobalTimeout || type == UpstreamResetType::PerTryTimeout) {
      callbacks_->requestInfo().onFailedResponse(
          Http::AccessLog::FailureReason::UpstreamRequestTimeout);

      code = Http::Code::GatewayTimeout;
      body = "upstream request timeout";
    } else {
      Http::AccessLog::FailureReason failure_reason =
          streamResetReasonToFailureReason(reset_reason.value());
      callbacks_->requestInfo().onFailedResponse(failure_reason);
      code = Http::Code::ServiceUnavailable;
      body = "upstream connect error or disconnect/reset before headers";
    }

    chargeUpstreamCode(code);
    Http::Utility::sendLocalReply(*callbacks_, code, body);
  }
}

Http::AccessLog::FailureReason
Filter::streamResetReasonToFailureReason(Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::ConnectionFailure:
    return Http::AccessLog::FailureReason::UpstreamConnectionFailure;
  case Http::StreamResetReason::ConnectionTermination:
    return Http::AccessLog::FailureReason::UpstreamConnectionTermination;
  case Http::StreamResetReason::LocalReset:
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return Http::AccessLog::FailureReason::LocalReset;
  case Http::StreamResetReason::Overflow:
    return Http::AccessLog::FailureReason::UpstreamOverflow;
  case Http::StreamResetReason::RemoteReset:
  case Http::StreamResetReason::RemoteRefusedStreamReset:
    return Http::AccessLog::FailureReason::UpstreamRemoteReset;
  }

  throw std::invalid_argument("Unknown reset_reason");
}

void Filter::onUpstreamHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!downstream_response_started_);

  if (retry_state_->shouldRetry(headers.get(), Optional<Http::StreamResetReason>(),
                                [this]() -> void { doRetry(); }) &&
      setupRetry(end_stream)) {
    Http::CodeUtility::chargeBasicResponseStat(
        config_.stats_store_, stat_prefix_ + "retry.",
        static_cast<Http::Code>(Http::Utility::getResponseStatus(*headers)));
    return;
  } else {
    // Make sure any retry timers are destroyed since we may not call cleanup() if end_stream is
    // false.
    retry_state_.reset();
  }

  // Only send upstream service time if we received the complete request and this is not a
  // premature response.
  if (DateUtil::timePointValid(upstream_request_->upstream_encoder_->requestCompleteTime())) {
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() -
        upstream_request_->upstream_encoder_->requestCompleteTime());
    headers->replaceViaMoveValue(Http::Headers::get().EnvoyUpstreamServiceTime,
                                 std::to_string(ms.count()));
  }

  upstream_request_->upstream_canary_ =
      (headers->get(Http::Headers::get().EnvoyUpstreamCanary) == "true") ||
      (upstream_host_ ? upstream_host_->canary() : false);
  chargeUpstreamCode(*headers);

  downstream_response_started_ = true;
  if (end_stream) {
    onUpstreamComplete();
  }

  callbacks_->encodeHeaders(std::move(headers), end_stream);
}

void Filter::onUpstreamData(const Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    onUpstreamComplete();
  }

  Buffer::OwnedImpl copy(data);
  callbacks_->encodeData(copy, end_stream);
}

void Filter::onUpstreamTrailers(Http::HeaderMapPtr&& trailers) {
  onUpstreamComplete();
  callbacks_->encodeTrailers(std::move(trailers));
}

void Filter::onUpstreamComplete() {
  if (!downstream_end_stream_) {
    upstream_request_->upstream_encoder_->resetStream();
  }

  if (config_.emit_dynamic_stats_ && !callbacks_->requestInfo().healthCheck() &&
      DateUtil::timePointValid(upstream_request_->upstream_encoder_->requestCompleteTime())) {
    std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() -
        upstream_request_->upstream_encoder_->requestCompleteTime());

    upstream_host_->outlierDetector().putResponseTime(response_time);

    Http::CodeUtility::ResponseTimingInfo info{
        config_.stats_store_, stat_prefix_, response_time, upstream_request_->upstream_canary_,
        downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true",
        route_->virtualHostName(), request_vcluster_ ? request_vcluster_->name() : "",
        config_.service_zone_, upstreamZone()};

    Http::CodeUtility::chargeResponseTiming(info);

    for (const std::string& alt_prefix : alt_stat_prefixes_) {
      Http::CodeUtility::ResponseTimingInfo info{
          config_.stats_store_, alt_prefix, response_time, upstream_request_->upstream_canary_,
          downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true", "", "",
          config_.service_zone_, upstreamZone()};

      Http::CodeUtility::chargeResponseTiming(info);
    }
  }

  cleanup();
}

bool Filter::setupRetry(bool end_stream) {
  // If we responded before the request was complete we don't bother doing a retry. This may not
  // catch certain cases where we are in full streaming mode and we have a connect timeout or an
  // overflow of some kind. However, in many cases deployments will use the buffer filter before
  // this filter which will make this a non-issue. The implementation of supporting retry in cases
  // where the request is not complete is more complicated so we will start with this for now.
  if (!downstream_end_stream_) {
    return false;
  }

  stream_log_debug("performing retry", *callbacks_);
  if (!end_stream) {
    upstream_request_->upstream_encoder_->resetStream();
  }

  upstream_request_.reset();
  return true;
}

void Filter::doRetry() {
  Http::ConnectionPool::Instance* conn_pool =
      config_.cm_.httpConnPoolForCluster(route_->clusterName(), finalPriority());
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    cleanup();
    return;
  }

  ASSERT(response_timeout_ || timeout_.global_timeout_.count() == 0);
  ASSERT(!upstream_request_);
  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool));
  upstream_request_->upstream_encoder_->encodeHeaders(
      *downstream_headers_, !callbacks_->decodingBuffer() && !downstream_trailers_);
  // It's possible we got immediately reset.
  if (upstream_request_) {
    if (callbacks_->decodingBuffer()) {
      upstream_request_->upstream_encoder_->encodeData(*callbacks_->decodingBuffer(),
                                                       !downstream_trailers_);
    }

    if (downstream_trailers_) {
      upstream_request_->upstream_encoder_->encodeTrailers(*downstream_trailers_);
    }

    upstream_request_->setupPerTryTimeout();
  }
}

Filter::UpstreamRequest::UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool)
    : parent_(parent),
      upstream_encoder_(new Http::PooledStreamEncoder(pool, *this, *this,
                                                      parent.callbacks_->connectionId(),
                                                      parent.callbacks_->streamId(), *this)) {}

Filter::UpstreamRequest::~UpstreamRequest() {
  if (per_try_timeout_) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
}

void Filter::UpstreamRequest::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  parent_.onUpstreamHeaders(std::move(headers), end_stream);
}

void Filter::UpstreamRequest::decodeData(const Buffer::Instance& data, bool end_stream) {
  parent_.onUpstreamData(data, end_stream);
}

void Filter::UpstreamRequest::decodeTrailers(Http::HeaderMapPtr&& trailers) {
  parent_.onUpstreamTrailers(std::move(trailers));
}

void Filter::UpstreamRequest::onResetStream(Http::StreamResetReason reason) {
  parent_.onUpstreamReset(UpstreamResetType::Reset, Optional<Http::StreamResetReason>(reason));
}

void Filter::UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout_.per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks_->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout_.per_try_timeout_);
  }
}

void Filter::UpstreamRequest::onPerTryTimeout() {
  stream_log_debug("upstream per try timeout", *parent_.callbacks_);
  parent_.config_.cm_.get(parent_.route_->clusterName())
      ->stats()
      .upstream_rq_per_try_timeout_.inc();
  upstream_encoder_->resetStream();
  parent_.onUpstreamReset(UpstreamResetType::PerTryTimeout,
                          Optional<Http::StreamResetReason>(Http::StreamResetReason::LocalReset));
}

RetryStatePtr
ProdFilter::createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                             const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                             Upstream::ResourcePriority priority) {
  return RetryStatePtr{
      new RetryStateImpl(policy, request_headers, cluster, runtime, random, dispatcher, priority)};
}

} // Router
