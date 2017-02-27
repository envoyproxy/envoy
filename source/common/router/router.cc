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
#include "common/http/utility.h"

namespace Router {

void FilterUtility::setUpstreamScheme(Http::HeaderMap& headers,
                                      const Upstream::ClusterInfo& cluster) {
  if (cluster.sslContext()) {
    headers.insertScheme().value(Http::Headers::get().SchemeValues.Https);
  } else {
    headers.insertScheme().value(Http::Headers::get().SchemeValues.Http);
  }
}

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
  Http::HeaderEntry* header_timeout_entry = request_headers.EnvoyUpstreamRequestTimeoutMs();
  uint64_t header_timeout;
  if (header_timeout_entry) {
    if (StringUtil::atoul(header_timeout_entry->value().c_str(), header_timeout)) {
      timeout.global_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.removeEnvoyUpstreamRequestTimeoutMs();
  }

  // See if there is a per try/retry timeout. If it's >= global we just ignore it.
  Http::HeaderEntry* per_try_timeout_entry = request_headers.EnvoyUpstreamRequestPerTryTimeoutMs();
  if (per_try_timeout_entry) {
    if (StringUtil::atoul(per_try_timeout_entry->value().c_str(), header_timeout)) {
      timeout.per_try_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
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
    request_headers.insertEnvoyExpectedRequestTimeoutMs().value(expected_timeout);
  }

  return timeout;
}

Filter::~Filter() {
  // Upstream resources should already have been cleaned.
  ASSERT(!upstream_request_);
  ASSERT(!retry_state_);
}

const std::string& Filter::upstreamZone(Upstream::HostDescriptionPtr upstream_host) {
  return upstream_host ? upstream_host->zone() : EMPTY_STRING;
}

void Filter::chargeUpstreamCode(const Http::HeaderMap& response_headers,
                                Upstream::HostDescriptionPtr upstream_host) {
  if (config_.emit_dynamic_stats_ && !callbacks_->requestInfo().healthCheck()) {
    const Http::HeaderEntry* upstream_canary_header = response_headers.EnvoyUpstreamCanary();
    const Http::HeaderEntry* internal_request_header = downstream_headers_->EnvoyInternalRequest();

    bool is_canary = (upstream_canary_header && upstream_canary_header->value() == "true") ||
                     (upstream_host ? upstream_host->canary() : false);
    bool internal_request = internal_request_header && internal_request_header->value() == "true";

    Http::CodeUtility::ResponseStatInfo info{
        config_.global_store_, cluster_->statsScope(), EMPTY_STRING, response_headers,
        internal_request, route_entry_->virtualHost().name(),
        request_vcluster_ ? request_vcluster_->name() : EMPTY_STRING,
        config_.local_info_.zoneName(), upstreamZone(upstream_host), is_canary};

    Http::CodeUtility::chargeResponseStat(info);

    if (!alt_stat_prefix_.empty()) {
      Http::CodeUtility::ResponseStatInfo info{
          config_.global_store_, cluster_->statsScope(), alt_stat_prefix_, response_headers,
          internal_request, EMPTY_STRING, EMPTY_STRING, config_.local_info_.zoneName(),
          upstreamZone(upstream_host), is_canary};

      Http::CodeUtility::chargeResponseStat(info);
    }
  }
}

void Filter::chargeUpstreamCode(Http::Code code, Upstream::HostDescriptionPtr upstream_host) {
  Http::HeaderMapImpl fake_response_headers{
      {Http::Headers::get().Status, std::to_string(enumToInt(code))}};
  chargeUpstreamCode(fake_response_headers, upstream_host);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  downstream_headers_ = &headers;

  // Only increment rq total stat if we actually decode headers here. This does not count requests
  // that get handled by earlier filters.
  config_.stats_.rq_total_.inc();

  // Determine if there is a route entry or a redirect for the request.
  route_ = callbacks_->route();
  if (!route_) {
    config_.stats_.no_route_.inc();
    stream_log_debug("no cluster match for URL '{}'", *callbacks_, headers.Path()->value().c_str());

    callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::NoRouteFound);
    Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::NotFound))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Determine if there is a redirect for the request.
  if (route_->redirectEntry()) {
    config_.stats_.rq_redirect_.inc();
    Http::Utility::sendRedirect(*callbacks_, route_->redirectEntry()->newPath(headers));
    return Http::FilterHeadersStatus::StopIteration;
  }

  // A route entry matches for the request.
  route_entry_ = route_->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_.cm_.get(route_entry_->clusterName());
  if (!cluster) {
    config_.stats_.no_cluster_.inc();
    stream_log_debug("unknown cluster '{}'", *callbacks_, route_entry_->clusterName());

    callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::NoRouteFound);
    Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::NotFound))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    return Http::FilterHeadersStatus::StopIteration;
  }
  cluster_ = cluster->info();

  // Set up stat prefixes, etc.
  request_vcluster_ = route_entry_->virtualCluster(headers);
  stream_log_debug("cluster '{}' match for URL '{}'", *callbacks_, route_entry_->clusterName(),
                   headers.Path()->value().c_str());

  const Http::HeaderEntry* request_alt_name = headers.EnvoyUpstreamAltStatName();
  if (request_alt_name) {
    alt_stat_prefix_ = std::string(request_alt_name->value().c_str()) + ".";
    headers.removeEnvoyUpstreamAltStatName();
  }

  // See if we are supposed to immediately kill some percentage of this cluster's traffic.
  if (cluster_->maintenanceMode()) {
    callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr);
    Http::Utility::sendLocalReply(*callbacks_, Http::Code::ServiceUnavailable, "maintenance mode");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // See if we need to set up for hashing.
  if (route_entry_->hashPolicy()) {
    Optional<uint64_t> hash = route_entry_->hashPolicy()->generateHash(headers);
    if (hash.valid()) {
      lb_context_.reset(new LoadBalancerContextImpl(hash));
    }
  }

  // Fetch a connection pool for the upstream cluster.
  Http::ConnectionPool::Instance* conn_pool = getConnPool();
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  timeout_ = FilterUtility::finalTimeout(*route_entry_, headers);

  // If this header is set with any value, use an alternate response code on timeout
  if (headers.EnvoyUpstreamRequestTimeoutAltResponse()) {
    timeout_response_code_ = Http::Code::NoContent;
    headers.removeEnvoyUpstreamRequestTimeoutAltResponse();
  }

  route_entry_->finalizeRequestHeaders(headers);
  FilterUtility::setUpstreamScheme(headers, *cluster_);
  retry_state_ = createRetryState(route_entry_->retryPolicy(), headers, *cluster_, config_.runtime_,
                                  config_.random_, callbacks_->dispatcher(), finalPriority());
  do_shadowing_ = FilterUtility::shouldShadow(route_entry_->shadowPolicy(), config_.runtime_,
                                              callbacks_->streamId());

#ifndef NDEBUG
  headers.iterate([](const Http::HeaderEntry& header, void* context) -> void {
    stream_log_debug("  '{}':'{}'", *static_cast<Http::StreamDecoderFilterCallbacks*>(context),
                     header.key().c_str(), header.value().c_str());
  }, callbacks_);
#endif

  // Do a common header check. We make sure that all outgoing requests have all HTTP/2 headers.
  // These get stripped by HTTP/1 codec where applicable.
  ASSERT(headers.Scheme());
  ASSERT(headers.Method());
  ASSERT(headers.Host());
  ASSERT(headers.Path());

  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool));
  upstream_request_->encodeHeaders(end_stream);
  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::ConnectionPool::Instance* Filter::getConnPool() {
  return config_.cm_.httpConnPoolForCluster(route_entry_->clusterName(), finalPriority(),
                                            lb_context_.get());
}

void Filter::sendNoHealthyUpstreamResponse() {
  callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::NoHealthyUpstream);
  chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr);
  Http::Utility::sendLocalReply(*callbacks_, Http::Code::ServiceUnavailable, "no healthy upstream");
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  bool buffering = (retry_state_ && retry_state_->enabled()) || do_shadowing_;

  // If we are going to buffer for retries or shadowing, we need to make a copy before encoding
  // since it's all moves from here on.
  if (buffering) {
    Buffer::OwnedImpl copy(data);
    upstream_request_->encodeData(copy, end_stream);
  } else {
    upstream_request_->encodeData(data, end_stream);
  }

  if (end_stream) {
    onRequestComplete();
  }

  // If we are potentially going to retry or shadow this request we need to buffer.
  return buffering ? Http::FilterDataStatus::StopIterationAndBuffer
                   : Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
  downstream_trailers_ = &trailers;
  upstream_request_->encodeTrailers(trailers);
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
    return route_entry_->priority();
  }
}

void Filter::maybeDoShadowing() {
  if (!do_shadowing_) {
    return;
  }

  ASSERT(!route_entry_->shadowPolicy().cluster().empty());
  Http::MessagePtr request(new Http::RequestMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_headers_)}));
  if (callbacks_->decodingBuffer()) {
    request->body(Buffer::InstancePtr{new Buffer::OwnedImpl(*callbacks_->decodingBuffer())});
  }
  if (downstream_trailers_) {
    request->trailers(Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_trailers_)});
  }

  config_.shadowWriter().shadow(route_entry_->shadowPolicy().cluster(), std::move(request),
                                timeout_.global_timeout_);
}

void Filter::onRequestComplete() {
  downstream_end_stream_ = true;
  downstream_request_complete_time_ = std::chrono::system_clock::now();

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
    upstream_request_->resetStream();
  }

  cleanup();
}

void Filter::onResponseTimeout() {
  stream_log_debug("upstream timeout", *callbacks_);
  cluster_->stats().upstream_rq_timeout_.inc();

  // It's possible to timeout during a retry backoff delay when we have no upstream request. In
  // this case we fake a reset since onUpstreamReset() doesn't care.
  if (upstream_request_) {
    if (upstream_request_->upstream_host_) {
      upstream_request_->upstream_host_->stats().rq_timeout_.inc();
    }
    upstream_request_->resetStream();
  }

  onUpstreamReset(UpstreamResetType::GlobalTimeout, Optional<Http::StreamResetReason>());
}

void Filter::onUpstreamReset(UpstreamResetType type,
                             const Optional<Http::StreamResetReason>& reset_reason) {
  ASSERT(type == UpstreamResetType::GlobalTimeout || upstream_request_);
  if (type == UpstreamResetType::Reset) {
    stream_log_debug("upstream reset", *callbacks_);
  }

  Upstream::HostDescriptionPtr upstream_host;
  if (upstream_request_) {
    upstream_host = upstream_request_->upstream_host_;
    if (upstream_host) {
      upstream_host->outlierDetector().putHttpResponseCode(
          enumToInt(type == UpstreamResetType::Reset ? Http::Code::ServiceUnavailable
                                                     : timeout_response_code_));
    }
  }

  // We don't retry on a global timeout or if we already started the response.
  if (type != UpstreamResetType::GlobalTimeout && !downstream_response_started_ && retry_state_ &&
      retry_state_->shouldRetry(nullptr, reset_reason, [this]() -> void { doRetry(); }) &&
      setupRetry(true)) {
    return;
  }

  // This will destroy any created retry timers.
  cleanup();

  // If we have not yet sent anything downstream, send a response with an appropriate status code.
  // Otherwise just reset the ongoing response.
  if (downstream_response_started_) {
    callbacks_->resetStream();
  } else {
    Http::Code code;
    const char* body;
    if (type == UpstreamResetType::GlobalTimeout || type == UpstreamResetType::PerTryTimeout) {
      callbacks_->requestInfo().setResponseFlag(
          Http::AccessLog::ResponseFlag::UpstreamRequestTimeout);

      code = timeout_response_code_;
      body = code == Http::Code::GatewayTimeout ? "upstream request timeout" : "";
    } else {
      Http::AccessLog::ResponseFlag response_flags =
          streamResetReasonToResponseFlag(reset_reason.value());
      callbacks_->requestInfo().setResponseFlag(response_flags);
      code = Http::Code::ServiceUnavailable;
      body = "upstream connect error or disconnect/reset before headers";
    }

    chargeUpstreamCode(code, upstream_host);
    Http::Utility::sendLocalReply(*callbacks_, code, body);
  }
}

Http::AccessLog::ResponseFlag
Filter::streamResetReasonToResponseFlag(Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::ConnectionFailure:
    return Http::AccessLog::ResponseFlag::UpstreamConnectionFailure;
  case Http::StreamResetReason::ConnectionTermination:
    return Http::AccessLog::ResponseFlag::UpstreamConnectionTermination;
  case Http::StreamResetReason::LocalReset:
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return Http::AccessLog::ResponseFlag::LocalReset;
  case Http::StreamResetReason::Overflow:
    return Http::AccessLog::ResponseFlag::UpstreamOverflow;
  case Http::StreamResetReason::RemoteReset:
  case Http::StreamResetReason::RemoteRefusedStreamReset:
    return Http::AccessLog::ResponseFlag::UpstreamRemoteReset;
  }

  throw std::invalid_argument("Unknown reset_reason");
}

void Filter::onUpstreamHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  stream_log_debug("upstream headers complete: end_stream={}", *callbacks_, end_stream);
  ASSERT(!downstream_response_started_);

  upstream_request_->upstream_host_->outlierDetector().putHttpResponseCode(
      Http::Utility::getResponseStatus(*headers));

  if (retry_state_ &&
      retry_state_->shouldRetry(headers.get(), Optional<Http::StreamResetReason>(),
                                [this]() -> void { doRetry(); }) &&
      setupRetry(end_stream)) {
    Http::CodeUtility::chargeBasicResponseStat(
        cluster_->statsScope(), "retry.",
        static_cast<Http::Code>(Http::Utility::getResponseStatus(*headers)));
    return;
  } else {
    // Make sure any retry timers are destroyed since we may not call cleanup() if end_stream is
    // false.
    retry_state_.reset();
  }

  // Only send upstream service time if we received the complete request and this is not a
  // premature response.
  if (DateUtil::timePointValid(downstream_request_complete_time_)) {
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - downstream_request_complete_time_);
    headers->insertEnvoyUpstreamServiceTime().value(ms.count());
  }

  upstream_request_->upstream_canary_ =
      (headers->EnvoyUpstreamCanary() && headers->EnvoyUpstreamCanary()->value() == "true") ||
      upstream_request_->upstream_host_->canary();
  chargeUpstreamCode(*headers, upstream_request_->upstream_host_);

  downstream_response_started_ = true;
  if (end_stream) {
    onUpstreamComplete();
  }

  callbacks_->encodeHeaders(std::move(headers), end_stream);
}

void Filter::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    onUpstreamComplete();
  }

  callbacks_->encodeData(data, end_stream);
}

void Filter::onUpstreamTrailers(Http::HeaderMapPtr&& trailers) {
  onUpstreamComplete();
  callbacks_->encodeTrailers(std::move(trailers));
}

void Filter::onUpstreamComplete() {
  if (!downstream_end_stream_) {
    upstream_request_->resetStream();
  }

  if (config_.emit_dynamic_stats_ && !callbacks_->requestInfo().healthCheck() &&
      DateUtil::timePointValid(downstream_request_complete_time_)) {
    std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - downstream_request_complete_time_);

    upstream_request_->upstream_host_->outlierDetector().putResponseTime(response_time);

    const Http::HeaderEntry* internal_request_header = downstream_headers_->EnvoyInternalRequest();
    bool internal_request = internal_request_header && internal_request_header->value() == "true";

    Http::CodeUtility::ResponseTimingInfo info{
        config_.global_store_, cluster_->statsScope(), EMPTY_STRING, response_time,
        upstream_request_->upstream_canary_, internal_request, route_entry_->virtualHost().name(),
        request_vcluster_ ? request_vcluster_->name() : EMPTY_STRING,
        config_.local_info_.zoneName(), upstreamZone(upstream_request_->upstream_host_)};

    Http::CodeUtility::chargeResponseTiming(info);

    if (!alt_stat_prefix_.empty()) {
      Http::CodeUtility::ResponseTimingInfo info{
          config_.global_store_, cluster_->statsScope(), alt_stat_prefix_, response_time,
          upstream_request_->upstream_canary_, internal_request, EMPTY_STRING, EMPTY_STRING,
          config_.local_info_.zoneName(), upstreamZone(upstream_request_->upstream_host_)};

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
    upstream_request_->resetStream();
  }

  upstream_request_.reset();
  return true;
}

void Filter::doRetry() {
  Http::ConnectionPool::Instance* conn_pool = getConnPool();
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    cleanup();
    return;
  }

  ASSERT(response_timeout_ || timeout_.global_timeout_.count() == 0);
  ASSERT(!upstream_request_);
  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool));
  upstream_request_->encodeHeaders(!callbacks_->decodingBuffer() && !downstream_trailers_);
  // It's possible we got immediately reset.
  if (upstream_request_) {
    if (callbacks_->decodingBuffer()) {
      // If we are doing a retry we need to make a copy.
      Buffer::OwnedImpl copy(*callbacks_->decodingBuffer());
      upstream_request_->encodeData(copy, !downstream_trailers_);
    }

    if (downstream_trailers_) {
      upstream_request_->encodeTrailers(*downstream_trailers_);
    }

    upstream_request_->setupPerTryTimeout();
  }
}

Filter::UpstreamRequest::~UpstreamRequest() {
  if (per_try_timeout_) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
}

void Filter::UpstreamRequest::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  parent_.onUpstreamHeaders(std::move(headers), end_stream);
}

void Filter::UpstreamRequest::decodeData(Buffer::Instance& data, bool end_stream) {
  parent_.onUpstreamData(data, end_stream);
}

void Filter::UpstreamRequest::decodeTrailers(Http::HeaderMapPtr&& trailers) {
  parent_.onUpstreamTrailers(std::move(trailers));
}

void Filter::UpstreamRequest::encodeHeaders(bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  // It's possible for a reset to happen inline within the newStream() call. In this case, we might
  // get deleted inline as well. Only write the returned handle out if it is not nullptr to deal
  // with this case.
  Http::ConnectionPool::Cancellable* handle = conn_pool_.newStream(*this, *this);
  if (handle) {
    conn_pool_stream_handle_ = handle;
  }
}

void Filter::UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  if (!request_encoder_) {
    stream_log_trace("buffering {} bytes", *parent_.callbacks_, data.length());
    if (!buffered_request_body_) {
      buffered_request_body_.reset(new Buffer::OwnedImpl());
    }

    buffered_request_body_->move(data);
  } else {
    stream_log_trace("proxying {} bytes", *parent_.callbacks_, data.length());
    request_encoder_->encodeData(data, end_stream);
  }
}

void Filter::UpstreamRequest::encodeTrailers(const Http::HeaderMap& trailers) {
  ASSERT(!encode_complete_);
  encode_complete_ = true;
  encode_trailers_ = true;

  if (!request_encoder_) {
    stream_log_trace("buffering trailers", *parent_.callbacks_);
  } else {
    stream_log_trace("proxying trailers", *parent_.callbacks_);
    request_encoder_->encodeTrailers(trailers);
  }
}

void Filter::UpstreamRequest::onResetStream(Http::StreamResetReason reason) {
  request_encoder_ = nullptr;
  if (!calling_encode_headers_) {
    parent_.onUpstreamReset(UpstreamResetType::Reset, Optional<Http::StreamResetReason>(reason));
  } else {
    deferred_reset_reason_ = reason;
  }
}

void Filter::UpstreamRequest::resetStream() {
  if (conn_pool_stream_handle_) {
    stream_log_debug("cancelling pool request", *parent_.callbacks_);
    ASSERT(!request_encoder_);
    conn_pool_stream_handle_->cancel();
    conn_pool_stream_handle_ = nullptr;
  }

  if (request_encoder_) {
    stream_log_debug("resetting pool request", *parent_.callbacks_);
    request_encoder_->getStream().removeCallbacks(*this);
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
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
  parent_.cluster_->stats().upstream_rq_per_try_timeout_.inc();
  upstream_host_->stats().rq_timeout_.inc();
  resetStream();
  parent_.onUpstreamReset(UpstreamResetType::PerTryTimeout,
                          Optional<Http::StreamResetReason>(Http::StreamResetReason::LocalReset));
}

void Filter::UpstreamRequest::onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                                            Upstream::HostDescriptionPtr host) {
  Http::StreamResetReason reset_reason = Http::StreamResetReason::ConnectionFailure;
  switch (reason) {
  case Http::ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = Http::StreamResetReason::Overflow;
    break;
  case Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
    break;
  }

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reset_reason);
}

void Filter::UpstreamRequest::onPoolReady(Http::StreamEncoder& request_encoder,
                                          Upstream::HostDescriptionPtr host) {
  stream_log_debug("pool ready", *parent_.callbacks_);
  onUpstreamHostSelected(host);
  request_encoder.getStream().addCallbacks(*this);

  conn_pool_stream_handle_ = nullptr;
  request_encoder_ = &request_encoder;
  calling_encode_headers_ = true;
  if (parent_.route_entry_->autoHostRewrite() && !host->hostname().empty()) {
    parent_.downstream_headers_->Host()->value(host->hostname());
  }

  request_encoder.encodeHeaders(*parent_.downstream_headers_,
                                !buffered_request_body_ && encode_complete_ && !encode_trailers_);
  calling_encode_headers_ = false;

  // It is possible to get reset in the middle of an encodeHeaders() call. This happens for example
  // in the http/2 codec if the frame cannot be encoded for some reason. This should never happen
  // but it's unclear if we have covered all cases so protect against it and test for it. One
  // specific example of a case where this happens is if we try to encode a total header size that
  // is too big in HTTP/2 (64K currently).
  if (deferred_reset_reason_.valid()) {
    onResetStream(deferred_reset_reason_.value());
  } else {
    if (buffered_request_body_) {
      request_encoder.encodeData(*buffered_request_body_, encode_complete_ && !encode_trailers_);
    }

    if (encode_trailers_) {
      request_encoder.encodeTrailers(*parent_.downstream_trailers_);
    }
  }
}

RetryStatePtr
ProdFilter::createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                             const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                             Upstream::ResourcePriority priority) {
  return RetryStateImpl::create(policy, request_headers, cluster, runtime, random, dispatcher,
                                priority);
}

} // Router
