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
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/pooled_stream_encoder.h"
#include "common/http/utility.h"

namespace Router {

std::chrono::milliseconds FilterUtility::finalTimeout(const RouteEntry& route,
                                                      Http::HeaderMap& request_headers) {
  // See if there is a user supplied timeout in a request header. If there is we take that,
  // otherwise we use the default.
  std::chrono::milliseconds timeout = route.timeout();
  const std::string& header_timeout_string =
      request_headers.get(Http::Headers::get().EnvoyUpstreamRequestTimeoutMs);
  uint64_t header_timeout;
  if (!header_timeout_string.empty()) {
    if (StringUtil::atoul(header_timeout_string.c_str(), header_timeout)) {
      timeout = std::chrono::milliseconds(header_timeout);
    }
    request_headers.remove(Http::Headers::get().EnvoyUpstreamRequestTimeoutMs);
  }

  if (!request_headers.has(Http::Headers::get().EnvoyExpectedRequestTimeoutMs)) {
    request_headers.addViaCopy(Http::Headers::get().EnvoyExpectedRequestTimeoutMs,
                               std::to_string(timeout.count()));
  }

  return timeout;
}

Filter::Filter(const std::string& stat_prefix, Stats::Store& stats, Upstream::ClusterManager& cm,
               Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : stats_store_(stats), cm_(cm), runtime_(runtime), random_(random),
      stats_{ALL_ROUTER_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix))} {}

Filter::~Filter() {
  // Upstream resources should already have been cleaned.
  ASSERT(!upstream_request_);
  ASSERT(!retry_state_);
}

void Filter::chargeUpstreamCode(const Http::HeaderMap& response_headers) {
  if (!callbacks_->requestInfo().healthCheck()) {
    Http::CodeUtility::ResponseStatInfo info{
        stats_store_, stat_prefix_, response_headers,
        downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true",
        route_->virtualHostName(), request_vcluster_name_};

    Http::CodeUtility::chargeResponseStat(info);

    for (const std::string& alt_prefix : alt_stat_prefixes_) {
      Http::CodeUtility::ResponseStatInfo info{
          stats_store_, alt_prefix, response_headers,
          downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true", "", ""};

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
  stats_.rq_total_.inc();

  // First determine if we need to do a redirect before we do anything else.
  const RedirectEntry* redirect = callbacks_->routeTable().redirectRequest(headers);
  if (redirect) {
    stats_.rq_redirect_.inc();
    Http::Utility::sendRedirect(*callbacks_, redirect->newPath(headers));
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Determine if there is a route match.
  route_ = callbacks_->routeTable().routeForRequest(headers);
  if (!route_) {
    stats_.no_route_.inc();
    stream_log_debug("no cluster match for URL '{}'", *callbacks_,
                     headers.get(Http::Headers::get().Path));

    callbacks_->requestInfo().onFailedResponse(Http::AccessLog::FailureReason::NoRouteFound);
    Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::NotFound))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Set up stat prefixes, etc.
  request_vcluster_name_ = route_->virtualClusterName(headers);
  stat_prefix_ = fmt::format("cluster.{}.", route_->clusterName());
  stream_log_debug("cluster '{}' match for URL '{}'", *callbacks_, route_->clusterName(),
                   headers.get(Http::Headers::get().Path));

  const Upstream::Cluster& cluster = *cm_.get(route_->clusterName());
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
  if (runtime_.snapshot().featureEnabled(
          fmt::format("upstream.maintenance_mode.{}", route_->clusterName()), 0)) {
    callbacks_->requestInfo().onFailedResponse(Http::AccessLog::FailureReason::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable);
    Http::Utility::sendLocalReply(*callbacks_, Http::Code::ServiceUnavailable, "maintenance mode");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Fetch a connection pool for the upstream cluster.
  Http::ConnectionPool::Instance* conn_pool = cm_.httpConnPoolForCluster(route_->clusterName());
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  timeout_ = FilterUtility::finalTimeout(*route_, headers);
  route_->finalizeRequestHeaders(headers);
  retry_state_ = createRetryState(route_->retryPolicy(), headers, cluster, runtime_, random_,
                                  callbacks_->dispatcher());

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

  // If we are potentially going to retry this request we need to buffer.
  return retry_state_->enabled() ? Http::FilterDataStatus::StopIterationAndBuffer
                                 : Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
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

void Filter::onRequestComplete() {
  downstream_end_stream_ = true;

  // Possible that we got an immediate reset.
  if (upstream_request_ && timeout_.count() > 0) {
    response_timeout_ =
        callbacks_->dispatcher().createTimer([this]() -> void { onResponseTimeout(); });
    response_timeout_->enableTimer(timeout_);
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
  cm_.get(route_->clusterName())->stats().upstream_rq_timeout_.inc();

  // It's possible to timeout during a retry backoff delay when we have no upstream request. In
  // this case we fake a reset since onUpstreamReset() doesn't care.
  if (upstream_request_) {
    upstream_request_->upstream_encoder_->resetStream();
  }

  onUpstreamReset(true, Optional<Http::StreamResetReason>());
}

void Filter::onUpstreamReset(bool timeout, const Optional<Http::StreamResetReason>& reset_reason) {
  ASSERT(timeout || upstream_request_);
  if (!timeout) {
    stream_log_debug("upstream reset", *callbacks_);
  }

  // We don't retry on a timeout or if we already started the response.
  if (!timeout && !downstream_response_started_ &&
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
    if (timeout) {
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
    return Http::AccessLog::FailureReason::LocalReset;
  case Http::StreamResetReason::Overflow:
    return Http::AccessLog::FailureReason::UpstreamOverflow;
  case Http::StreamResetReason::RemoteReset:
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
        stats_store_, stat_prefix_ + "retry.",
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

  // TODO: Check host's canary status in addition to canary header.
  upstream_request_->upstream_canary_ =
      headers->get(Http::Headers::get().EnvoyUpstreamCanary) == "true";
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

  if (!callbacks_->requestInfo().healthCheck()) {
    Http::CodeUtility::ResponseTimingInfo info{
        stats_store_, stat_prefix_, upstream_request_->upstream_encoder_->requestCompleteTime(),
        upstream_request_->upstream_canary_,
        downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true",
        route_->virtualHostName(), request_vcluster_name_};

    Http::CodeUtility::chargeResponseTiming(info);

    for (const std::string& alt_prefix : alt_stat_prefixes_) {
      Http::CodeUtility::ResponseTimingInfo info{
          stats_store_, alt_prefix, upstream_request_->upstream_encoder_->requestCompleteTime(),
          upstream_request_->upstream_canary_,
          downstream_headers_->get(Http::Headers::get().EnvoyInternalRequest) == "true", "", ""};

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
  Http::ConnectionPool::Instance* conn_pool = cm_.httpConnPoolForCluster(route_->clusterName());
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    cleanup();
    return;
  }

  ASSERT(response_timeout_ || timeout_.count() == 0);
  ASSERT(!upstream_request_);
  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool));
  upstream_request_->upstream_encoder_->encodeHeaders(*downstream_headers_,
                                                      !callbacks_->decodingBuffer());
  // It's possible we got immediately reset.
  if (upstream_request_ && callbacks_->decodingBuffer()) {
    upstream_request_->upstream_encoder_->encodeData(*callbacks_->decodingBuffer(), true);
  }
}

Filter::UpstreamRequest::UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool)
    : parent_(parent),
      upstream_encoder_(new Http::PooledStreamEncoder(pool, *this, *this,
                                                      parent.callbacks_->connectionId(),
                                                      parent.callbacks_->streamId(), *this)) {}

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
  parent_.onUpstreamReset(false, Optional<Http::StreamResetReason>(reason));
}

RetryStatePtr
ProdFilter::createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                             const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher) {
  return RetryStatePtr{
      new RetryStateImpl(policy, request_headers, cluster, runtime, random, dispatcher)};
}

} // Router
