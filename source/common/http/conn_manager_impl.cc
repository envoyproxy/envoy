#include "common/http/conn_manager_impl.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/drain_decision.h"
#include "envoy/router/router.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/scope_tracker.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/conn_manager_utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/path_utility.h"
#include "common/http/status.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/router/config_impl.h"
#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/timespan_impl.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

ConnectionManagerStats ConnectionManagerImpl::generateStats(const std::string& prefix,
                                                            Stats::Scope& scope) {
  return ConnectionManagerStats(
      {ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix),
                               POOL_HISTOGRAM_PREFIX(scope, prefix))},
      prefix, scope);
}

ConnectionManagerTracingStats ConnectionManagerImpl::generateTracingStats(const std::string& prefix,
                                                                          Stats::Scope& scope) {
  return {CONN_MAN_TRACING_STATS(POOL_COUNTER_PREFIX(scope, prefix + "tracing."))};
}

ConnectionManagerListenerStats
ConnectionManagerImpl::generateListenerStats(const std::string& prefix, Stats::Scope& scope) {
  return {CONN_MAN_LISTENER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

ConnectionManagerImpl::ConnectionManagerImpl(ConnectionManagerConfig& config,
                                             const Network::DrainDecision& drain_close,
                                             Random::RandomGenerator& random_generator,
                                             Http::Context& http_context, Runtime::Loader& runtime,
                                             const LocalInfo::LocalInfo& local_info,
                                             Upstream::ClusterManager& cluster_manager,
                                             Server::OverloadManager& overload_manager,
                                             TimeSource& time_source)
    : config_(config), stats_(config_.stats()),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          stats_.named_.downstream_cx_length_ms_, time_source)),
      drain_close_(drain_close), user_agent_(http_context.userAgentContext()),
      random_generator_(random_generator), http_context_(http_context), runtime_(runtime),
      local_info_(local_info), cluster_manager_(cluster_manager),
      listener_stats_(config_.listenerStats()),
      overload_stop_accepting_requests_ref_(overload_manager.getThreadLocalOverloadState().getState(
          Server::OverloadActionNames::get().StopAcceptingRequests)),
      overload_disable_keepalive_ref_(overload_manager.getThreadLocalOverloadState().getState(
          Server::OverloadActionNames::get().DisableHttpKeepAlive)),
      time_source_(time_source) {}

const ResponseHeaderMap& ConnectionManagerImpl::continueHeader() {
  static const auto headers = createHeaderMap<ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Code::Continue))}});
  return *headers;
}

void ConnectionManagerImpl::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  stats_.named_.downstream_cx_total_.inc();
  stats_.named_.downstream_cx_active_.inc();
  if (read_callbacks_->connection().ssl()) {
    stats_.named_.downstream_cx_ssl_total_.inc();
    stats_.named_.downstream_cx_ssl_active_.inc();
  }

  read_callbacks_->connection().addConnectionCallbacks(*this);

  if (config_.idleTimeout()) {
    connection_idle_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onIdleTimeout(); });
    connection_idle_timer_->enableTimer(config_.idleTimeout().value());
  }

  if (config_.maxConnectionDuration()) {
    connection_duration_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onConnectionDurationTimeout(); });
    connection_duration_timer_->enableTimer(config_.maxConnectionDuration().value());
  }

  read_callbacks_->connection().setDelayedCloseTimeout(config_.delayedCloseTimeout());

  read_callbacks_->connection().setConnectionStats(
      {stats_.named_.downstream_cx_rx_bytes_total_, stats_.named_.downstream_cx_rx_bytes_buffered_,
       stats_.named_.downstream_cx_tx_bytes_total_, stats_.named_.downstream_cx_tx_bytes_buffered_,
       nullptr, &stats_.named_.downstream_cx_delayed_close_timeout_});
}

ConnectionManagerImpl::~ConnectionManagerImpl() {
  stats_.named_.downstream_cx_destroy_.inc();

  stats_.named_.downstream_cx_active_.dec();
  if (read_callbacks_->connection().ssl()) {
    stats_.named_.downstream_cx_ssl_active_.dec();
  }

  if (codec_) {
    if (codec_->protocol() == Protocol::Http2) {
      stats_.named_.downstream_cx_http2_active_.dec();
    } else if (codec_->protocol() == Protocol::Http3) {
      stats_.named_.downstream_cx_http3_active_.dec();
    } else {
      stats_.named_.downstream_cx_http1_active_.dec();
    }
  }

  conn_length_->complete();
  user_agent_.completeConnectionLength(*conn_length_);
}

void ConnectionManagerImpl::checkForDeferredClose() {
  if (drain_state_ == DrainState::Closing && streams_.empty() && !codec_->wantsToWrite()) {
    doConnectionClose(Network::ConnectionCloseType::FlushWriteAndDelay, absl::nullopt,
                      StreamInfo::ResponseCodeDetails::get().DownstreamLocalDisconnect);
  }
}

void ConnectionManagerImpl::doEndStream(ActiveStream& stream) {
  // The order of what happens in this routine is important and a little complicated. We first see
  // if the stream needs to be reset. If it needs to be, this will end up invoking reset callbacks
  // and then moving the stream to the deferred destruction list. If the stream has not been reset,
  // we move it to the deferred deletion list here. Then, we potentially close the connection. This
  // must be done after deleting the stream since the stream refers to the connection and must be
  // deleted first.
  bool reset_stream = false;
  // If the response encoder is still associated with the stream, reset the stream. The exception
  // here is when Envoy "ends" the stream by calling recreateStream at which point recreateStream
  // explicitly nulls out response_encoder to avoid the downstream being notified of the
  // Envoy-internal stream instance being ended.
  if (stream.response_encoder_ != nullptr &&
      (!stream.filter_manager_.remoteComplete() || !stream.state_.codec_saw_local_complete_)) {
    // Indicate local is complete at this point so that if we reset during a continuation, we don't
    // raise further data or trailers.
    ENVOY_STREAM_LOG(debug, "doEndStream() resetting stream", stream);
    // TODO(snowp): This call might not be necessary, try to clean up + remove setter function.
    stream.filter_manager_.setLocalComplete();
    stream.state_.codec_saw_local_complete_ = true;
    stream.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
    reset_stream = true;
  }

  if (!reset_stream) {
    doDeferredStreamDestroy(stream);
  }

  if (reset_stream && codec_->protocol() < Protocol::Http2) {
    drain_state_ = DrainState::Closing;
  }

  checkForDeferredClose();
}

void ConnectionManagerImpl::doDeferredStreamDestroy(ActiveStream& stream) {
  if (stream.max_stream_duration_timer_) {
    stream.max_stream_duration_timer_->disableTimer();
    stream.max_stream_duration_timer_ = nullptr;
  }
  if (stream.stream_idle_timer_ != nullptr) {
    stream.stream_idle_timer_->disableTimer();
    stream.stream_idle_timer_ = nullptr;
  }
  stream.filter_manager_.disarmRequestTimeout();

  stream.filter_manager_.destroyFilters();

  read_callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(streams_));

  if (connection_idle_timer_ && streams_.empty()) {
    connection_idle_timer_->enableTimer(config_.idleTimeout().value());
  }
}

RequestDecoder& ConnectionManagerImpl::newStream(ResponseEncoder& response_encoder,
                                                 bool is_internally_created) {
  if (connection_idle_timer_) {
    connection_idle_timer_->disableTimer();
  }

  ENVOY_CONN_LOG(debug, "new stream", read_callbacks_->connection());
  ActiveStreamPtr new_stream(new ActiveStream(*this, response_encoder.getStream().bufferLimit()));
  new_stream->state_.is_internally_created_ = is_internally_created;
  new_stream->response_encoder_ = &response_encoder;
  new_stream->response_encoder_->getStream().addCallbacks(*new_stream);
  new_stream->response_encoder_->getStream().setFlushTimeout(new_stream->idle_timeout_ms_);
  // If the network connection is backed up, the stream should be made aware of it on creation.
  // Both HTTP/1.x and HTTP/2 codecs handle this in StreamCallbackHelper::addCallbacksHelper.
  ASSERT(read_callbacks_->connection().aboveHighWatermark() == false ||
         new_stream->filter_manager_.aboveHighWatermark());
  LinkedList::moveIntoList(std::move(new_stream), streams_);
  return **streams_.begin();
}

void ConnectionManagerImpl::handleCodecError(absl::string_view error) {
  ENVOY_CONN_LOG(debug, "dispatch error: {}", read_callbacks_->connection(), error);
  read_callbacks_->connection().streamInfo().setResponseFlag(
      StreamInfo::ResponseFlag::DownstreamProtocolError);

  // HTTP/1.1 codec has already sent a 400 response if possible. HTTP/2 codec has already sent
  // GOAWAY.
  doConnectionClose(Network::ConnectionCloseType::FlushWriteAndDelay,
                    StreamInfo::ResponseFlag::DownstreamProtocolError,
                    absl::StrCat("codec error: ", error));
}

void ConnectionManagerImpl::createCodec(Buffer::Instance& data) {
  ASSERT(!codec_);
  codec_ = config_.createCodec(read_callbacks_->connection(), data, *this);

  switch (codec_->protocol()) {
  case Protocol::Http3:
    stats_.named_.downstream_cx_http3_total_.inc();
    stats_.named_.downstream_cx_http3_active_.inc();
    break;
  case Protocol::Http2:
    stats_.named_.downstream_cx_http2_total_.inc();
    stats_.named_.downstream_cx_http2_active_.inc();
    break;
  case Protocol::Http11:
  case Protocol::Http10:
    stats_.named_.downstream_cx_http1_total_.inc();
    stats_.named_.downstream_cx_http1_active_.inc();
    break;
  }
}

Network::FilterStatus ConnectionManagerImpl::onData(Buffer::Instance& data, bool) {
  if (!codec_) {
    // Http3 codec should have been instantiated by now.
    createCodec(data);
  }

  bool redispatch;
  do {
    redispatch = false;

    const Status status = codec_->dispatch(data);

    ASSERT(!isPrematureResponseError(status));
    if (isBufferFloodError(status)) {
      handleCodecError(status.message());
      return Network::FilterStatus::StopIteration;
    } else if (isCodecProtocolError(status)) {
      stats_.named_.downstream_cx_protocol_error_.inc();
      handleCodecError(status.message());
      return Network::FilterStatus::StopIteration;
    }

    // Processing incoming data may release outbound data so check for closure here as well.
    checkForDeferredClose();

    // The HTTP/1 codec will pause dispatch after a single message is complete. We want to
    // either redispatch if there are no streams and we have more data. If we have a single
    // complete non-WebSocket stream but have not responded yet we will pause socket reads
    // to apply back pressure.
    if (codec_->protocol() < Protocol::Http2) {
      if (read_callbacks_->connection().state() == Network::Connection::State::Open &&
          data.length() > 0 && streams_.empty()) {
        redispatch = true;
      }
    }
  } while (redispatch);

  if (!read_callbacks_->connection().streamInfo().protocol()) {
    read_callbacks_->connection().streamInfo().protocol(codec_->protocol());
  }

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus ConnectionManagerImpl::onNewConnection() {
  if (!read_callbacks_->connection().streamInfo().protocol()) {
    // For Non-QUIC traffic, continue passing data to filters.
    return Network::FilterStatus::Continue;
  }
  // Only QUIC connection's stream_info_ specifies protocol.
  Buffer::OwnedImpl dummy;
  createCodec(dummy);
  ASSERT(codec_->protocol() == Protocol::Http3);
  // Stop iterating through each filters for QUIC. Currently a QUIC connection
  // only supports one filter, HCM, and bypasses the onData() interface. Because
  // QUICHE already handles de-multiplexing.
  return Network::FilterStatus::StopIteration;
}

void ConnectionManagerImpl::resetAllStreams(absl::optional<StreamInfo::ResponseFlag> response_flag,
                                            absl::string_view details) {
  while (!streams_.empty()) {
    // Mimic a downstream reset in this case. We must also remove callbacks here. Though we are
    // about to close the connection and will disable further reads, it is possible that flushing
    // data out can cause stream callbacks to fire (e.g., low watermark callbacks).
    //
    // TODO(mattklein123): I tried to actually reset through the codec here, but ran into issues
    // with nghttp2 state and being unhappy about sending reset frames after the connection had
    // been terminated via GOAWAY. It might be possible to do something better here inside the h2
    // codec but there are no easy answers and this seems simpler.
    auto& stream = *streams_.front();
    stream.response_encoder_->getStream().removeCallbacks(stream);
    if (!stream.response_encoder_->getStream().responseDetails().empty()) {
      stream.filter_manager_.streamInfo().setResponseCodeDetails(
          stream.response_encoder_->getStream().responseDetails());
    } else if (!details.empty()) {
      stream.filter_manager_.streamInfo().setResponseCodeDetails(details);
    }
    if (response_flag.has_value()) {
      stream.filter_manager_.streamInfo().setResponseFlag(response_flag.value());
    }
    stream.onResetStream(StreamResetReason::ConnectionTermination, absl::string_view());
  }
}

void ConnectionManagerImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose) {
    stats_.named_.downstream_cx_destroy_local_.inc();
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (event == Network::ConnectionEvent::RemoteClose) {
      remote_close_ = true;
      stats_.named_.downstream_cx_destroy_remote_.inc();
    }
    absl::string_view details =
        event == Network::ConnectionEvent::RemoteClose
            ? StreamInfo::ResponseCodeDetails::get().DownstreamRemoteDisconnect
            : StreamInfo::ResponseCodeDetails::get().DownstreamLocalDisconnect;
    // TODO(mattklein123): It is technically possible that something outside of the filter causes
    // a local connection close, so we still guard against that here. A better solution would be to
    // have some type of "pre-close" callback that we could hook for cleanup that would get called
    // regardless of where local close is invoked from.
    // NOTE: that this will cause doConnectionClose() to get called twice in the common local close
    // cases, but the method protects against that.
    // NOTE: In the case where a local close comes from outside the filter, this will cause any
    // stream closures to increment remote close stats. We should do better here in the future,
    // via the pre-close callback mentioned above.
    doConnectionClose(absl::nullopt, absl::nullopt, details);
  }
}

void ConnectionManagerImpl::doConnectionClose(
    absl::optional<Network::ConnectionCloseType> close_type,
    absl::optional<StreamInfo::ResponseFlag> response_flag, absl::string_view details) {
  if (connection_idle_timer_) {
    connection_idle_timer_->disableTimer();
    connection_idle_timer_.reset();
  }

  if (connection_duration_timer_) {
    connection_duration_timer_->disableTimer();
    connection_duration_timer_.reset();
  }

  if (drain_timer_) {
    drain_timer_->disableTimer();
    drain_timer_.reset();
  }

  if (!streams_.empty()) {
    const Network::ConnectionEvent event = close_type.has_value()
                                               ? Network::ConnectionEvent::LocalClose
                                               : Network::ConnectionEvent::RemoteClose;
    if (event == Network::ConnectionEvent::LocalClose) {
      stats_.named_.downstream_cx_destroy_local_active_rq_.inc();
    }
    if (event == Network::ConnectionEvent::RemoteClose) {
      stats_.named_.downstream_cx_destroy_remote_active_rq_.inc();
    }

    stats_.named_.downstream_cx_destroy_active_rq_.inc();
    user_agent_.onConnectionDestroy(event, true);
    // Note that resetAllStreams() does not actually write anything to the wire. It just resets
    // all upstream streams and their filter stacks. Thus, there are no issues around recursive
    // entry.
    resetAllStreams(response_flag, details);
  }

  if (close_type.has_value()) {
    read_callbacks_->connection().close(close_type.value());
  }
}

void ConnectionManagerImpl::onGoAway(GoAwayErrorCode) {
  // Currently we do nothing with remote go away frames. In the future we can decide to no longer
  // push resources if applicable.
}

void ConnectionManagerImpl::onIdleTimeout() {
  ENVOY_CONN_LOG(debug, "idle timeout", read_callbacks_->connection());
  stats_.named_.downstream_cx_idle_timeout_.inc();
  if (!codec_) {
    // No need to delay close after flushing since an idle timeout has already fired. Attempt to
    // write out buffered data one last time and issue a local close if successful.
    doConnectionClose(Network::ConnectionCloseType::FlushWrite, absl::nullopt, "");
  } else if (drain_state_ == DrainState::NotDraining) {
    startDrainSequence();
  }
}

void ConnectionManagerImpl::onConnectionDurationTimeout() {
  ENVOY_CONN_LOG(debug, "max connection duration reached", read_callbacks_->connection());
  stats_.named_.downstream_cx_max_duration_reached_.inc();
  if (!codec_) {
    // Attempt to write out buffered data one last time and issue a local close if successful.
    doConnectionClose(Network::ConnectionCloseType::FlushWrite, absl::nullopt,
                      StreamInfo::ResponseCodeDetails::get().DurationTimeout);
  } else if (drain_state_ == DrainState::NotDraining) {
    startDrainSequence();
  }
}

void ConnectionManagerImpl::onDrainTimeout() {
  ASSERT(drain_state_ != DrainState::NotDraining);
  codec_->goAway();
  drain_state_ = DrainState::Closing;
  checkForDeferredClose();
}

void ConnectionManagerImpl::chargeTracingStats(const Tracing::Reason& tracing_reason,
                                               ConnectionManagerTracingStats& tracing_stats) {
  switch (tracing_reason) {
  case Tracing::Reason::ClientForced:
    tracing_stats.client_enabled_.inc();
    break;
  case Tracing::Reason::NotTraceableRequestId:
    tracing_stats.not_traceable_.inc();
    break;
  case Tracing::Reason::Sampling:
    tracing_stats.random_sampling_.inc();
    break;
  case Tracing::Reason::ServiceForced:
    tracing_stats.service_forced_.inc();
    break;
  default:
    throw std::invalid_argument(
        absl::StrCat("invalid tracing reason, value: ", static_cast<int32_t>(tracing_reason)));
  }
}

void ConnectionManagerImpl::RdsRouteConfigUpdateRequester::requestRouteConfigUpdate(
    const std::string host_header, Event::Dispatcher& thread_local_dispatcher,
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  route_config_provider_->requestVirtualHostsUpdate(host_header, thread_local_dispatcher,
                                                    std::move(route_config_updated_cb));
}

ConnectionManagerImpl::ActiveStream::ActiveStream(ConnectionManagerImpl& connection_manager,
                                                  uint32_t buffer_limit)
    : connection_manager_(connection_manager),
      stream_id_(connection_manager.random_generator_.random()),
      filter_manager_(*this, connection_manager_.read_callbacks_->connection().dispatcher(),
                      connection_manager_.read_callbacks_->connection(), stream_id_,
                      connection_manager_.config_.proxy100Continue(), buffer_limit,
                      connection_manager_.config_.filterFactory(),
                      connection_manager_.config_.localReply(),
                      connection_manager_.codec_->protocol(), connection_manager_.timeSource(),
                      connection_manager_.read_callbacks_->connection().streamInfo().filterState(),
                      StreamInfo::FilterState::LifeSpan::Connection),
      request_response_timespan_(new Stats::HistogramCompletableTimespanImpl(
          connection_manager_.stats_.named_.downstream_rq_time_,
          connection_manager_.timeSource())) {
  ASSERT(!connection_manager.config_.isRoutable() ||
             ((connection_manager.config_.routeConfigProvider() == nullptr &&
               connection_manager.config_.scopedRouteConfigProvider() != nullptr) ||
              (connection_manager.config_.routeConfigProvider() != nullptr &&
               connection_manager.config_.scopedRouteConfigProvider() == nullptr)),
         "Either routeConfigProvider or scopedRouteConfigProvider should be set in "
         "ConnectionManagerImpl.");
  for (const AccessLog::InstanceSharedPtr& access_log : connection_manager_.config_.accessLogs()) {
    filter_manager_.addAccessLogHandler(access_log);
  }

  filter_manager_.streamInfo().setRequestIDExtension(
      connection_manager.config_.requestIDExtension());

  if (connection_manager_.config_.isRoutable() &&
      connection_manager.config_.routeConfigProvider() != nullptr) {
    route_config_update_requester_ =
        std::make_unique<ConnectionManagerImpl::RdsRouteConfigUpdateRequester>(
            connection_manager.config_.routeConfigProvider());
  } else if (connection_manager_.config_.isRoutable() &&
             connection_manager.config_.scopedRouteConfigProvider() != nullptr) {
    route_config_update_requester_ =
        std::make_unique<ConnectionManagerImpl::NullRouteConfigUpdateRequester>();
  }
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());

  connection_manager_.stats_.named_.downstream_rq_total_.inc();
  connection_manager_.stats_.named_.downstream_rq_active_.inc();
  if (connection_manager_.codec_->protocol() == Protocol::Http2) {
    connection_manager_.stats_.named_.downstream_rq_http2_total_.inc();
  } else if (connection_manager_.codec_->protocol() == Protocol::Http3) {
    connection_manager_.stats_.named_.downstream_rq_http3_total_.inc();
  } else {
    connection_manager_.stats_.named_.downstream_rq_http1_total_.inc();
  }
  filter_manager_.streamInfo().setDownstreamLocalAddress(
      connection_manager_.read_callbacks_->connection().localAddress());
  filter_manager_.streamInfo().setDownstreamDirectRemoteAddress(
      connection_manager_.read_callbacks_->connection().directRemoteAddress());
  // Initially, the downstream remote address is the source address of the
  // downstream connection. That can change later in the request's lifecycle,
  // based on XFF processing, but setting the downstream remote address here
  // prevents surprises for logging code in edge cases.
  filter_manager_.streamInfo().setDownstreamRemoteAddress(
      connection_manager_.read_callbacks_->connection().remoteAddress());

  filter_manager_.streamInfo().setDownstreamSslConnection(
      connection_manager_.read_callbacks_->connection().ssl());

  if (connection_manager_.config_.streamIdleTimeout().count()) {
    idle_timeout_ms_ = connection_manager_.config_.streamIdleTimeout();
    stream_idle_timer_ = connection_manager_.read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onIdleTimeout(); });
    resetIdleTimer();
  }

  if (connection_manager_.config_.requestTimeout().count()) {
    std::chrono::milliseconds request_timeout_ms_ = connection_manager_.config_.requestTimeout();
    request_timer_ = connection_manager.read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onRequestTimeout(); });
    request_timer_->enableTimer(request_timeout_ms_, this);
  }

  const auto max_stream_duration = connection_manager_.config_.maxStreamDuration();
  if (max_stream_duration.has_value() && max_stream_duration.value().count()) {
    max_stream_duration_timer_ =
        connection_manager.read_callbacks_->connection().dispatcher().createTimer(
            [this]() -> void { onStreamMaxDurationReached(); });
    max_stream_duration_timer_->enableTimer(connection_manager_.config_.maxStreamDuration().value(),
                                            this);
  }

  filter_manager_.streamInfo().setRequestedServerName(
      connection_manager_.read_callbacks_->connection().requestedServerName());
}

ConnectionManagerImpl::ActiveStream::~ActiveStream() {
  filter_manager_.streamInfo().onRequestComplete();
  Upstream::HostDescriptionConstSharedPtr upstream_host =
      connection_manager_.read_callbacks_->upstreamHost();

  if (upstream_host != nullptr) {
    Upstream::ClusterRequestResponseSizeStatsOptRef req_resp_stats =
        upstream_host->cluster().requestResponseSizeStats();
    if (req_resp_stats.has_value()) {
      req_resp_stats->get().upstream_rq_body_size_.recordValue(
          filter_manager_.streamInfo().bytesReceived());
      req_resp_stats->get().upstream_rs_body_size_.recordValue(
          filter_manager_.streamInfo().bytesSent());
    }
  }

  if (connection_manager_.remote_close_) {
    filter_manager_.streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().DownstreamRemoteDisconnect);
    filter_manager_.streamInfo().setResponseFlag(
        StreamInfo::ResponseFlag::DownstreamConnectionTermination);
  }
  if (connection_manager_.codec_->protocol() < Protocol::Http2) {
    // For HTTP/2 there are still some reset cases where details are not set.
    // For HTTP/1 there shouldn't be any. Regression-proof this.
    ASSERT(filter_manager_.streamInfo().responseCodeDetails().has_value());
  }

  connection_manager_.stats_.named_.downstream_rq_active_.dec();
  if (filter_manager_.streamInfo().healthCheck()) {
    connection_manager_.config_.tracingStats().health_check_.inc();
  }

  if (active_span_) {
    Tracing::HttpTracerUtility::finalizeDownstreamSpan(
        *active_span_, filter_manager_.requestHeaders(), filter_manager_.responseHeaders(),
        filter_manager_.responseTrailers(), filter_manager_.streamInfo(), *this);
  }
  if (state_.successful_upgrade_) {
    connection_manager_.stats_.named_.downstream_cx_upgrades_active_.dec();
  }
}

void ConnectionManagerImpl::ActiveStream::resetIdleTimer() {
  if (stream_idle_timer_ != nullptr) {
    // TODO(htuch): If this shows up in performance profiles, optimize by only
    // updating a timestamp here and doing periodic checks for idle timeouts
    // instead, or reducing the accuracy of timers.
    stream_idle_timer_->enableTimer(idle_timeout_ms_);
  }
}

void ConnectionManagerImpl::ActiveStream::onIdleTimeout() {
  connection_manager_.stats_.named_.downstream_rq_idle_timeout_.inc();
  // If headers have not been sent to the user, send a 408.
  if (filter_manager_.responseHeaders() != nullptr &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_response_for_timeout")) {
    // TODO(htuch): We could send trailers here with an x-envoy timeout header
    // or gRPC status code, and/or set H2 RST_STREAM error.
    filter_manager_.streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().StreamIdleTimeout);
    connection_manager_.doEndStream(*this);
  } else {
    // TODO(mattklein) this may result in multiple flags. This Ok?
    filter_manager_.streamInfo().setResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout);
    sendLocalReply(filter_manager_.requestHeaders() != nullptr &&
                       Grpc::Common::isGrpcRequestHeaders(*filter_manager_.requestHeaders()),
                   Http::Code::RequestTimeout, "stream timeout", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().StreamIdleTimeout);
  }
}

void ConnectionManagerImpl::ActiveStream::onRequestTimeout() {
  connection_manager_.stats_.named_.downstream_rq_timeout_.inc();
  sendLocalReply(filter_manager_.requestHeaders() != nullptr &&
                     Grpc::Common::isGrpcRequestHeaders(*filter_manager_.requestHeaders()),
                 Http::Code::RequestTimeout, "request timeout", nullptr, absl::nullopt,
                 StreamInfo::ResponseCodeDetails::get().RequestOverallTimeout);
}

void ConnectionManagerImpl::ActiveStream::onStreamMaxDurationReached() {
  ENVOY_STREAM_LOG(debug, "Stream max duration time reached", *this);
  connection_manager_.stats_.named_.downstream_rq_max_duration_reached_.inc();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_response_for_timeout")) {
    sendLocalReply(filter_manager_.requestHeaders() != nullptr &&
                       Grpc::Common::isGrpcRequestHeaders(*filter_manager_.requestHeaders()),
                   Http::Code::RequestTimeout, "downstream duration timeout", nullptr,
                   absl::nullopt, StreamInfo::ResponseCodeDetails::get().MaxDurationTimeout);
  } else {
    filter_manager_.streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().MaxDurationTimeout);
    connection_manager_.doEndStream(*this);
  }
}

void ConnectionManagerImpl::ActiveStream::chargeStats(const ResponseHeaderMap& headers) {
  uint64_t response_code = Utility::getResponseStatus(headers);
  filter_manager_.streamInfo().response_code_ = response_code;

  if (filter_manager_.streamInfo().health_check_request_) {
    return;
  }

  Upstream::HostDescriptionConstSharedPtr upstream_host =
      connection_manager_.read_callbacks_->upstreamHost();

  if (upstream_host != nullptr) {
    Upstream::ClusterRequestResponseSizeStatsOptRef req_resp_stats =
        upstream_host->cluster().requestResponseSizeStats();
    if (req_resp_stats.has_value()) {
      req_resp_stats->get().upstream_rs_headers_size_.recordValue(headers.byteSize());
    }
  }

  connection_manager_.stats_.named_.downstream_rq_completed_.inc();
  connection_manager_.listener_stats_.downstream_rq_completed_.inc();
  if (CodeUtility::is1xx(response_code)) {
    connection_manager_.stats_.named_.downstream_rq_1xx_.inc();
    connection_manager_.listener_stats_.downstream_rq_1xx_.inc();
  } else if (CodeUtility::is2xx(response_code)) {
    connection_manager_.stats_.named_.downstream_rq_2xx_.inc();
    connection_manager_.listener_stats_.downstream_rq_2xx_.inc();
  } else if (CodeUtility::is3xx(response_code)) {
    connection_manager_.stats_.named_.downstream_rq_3xx_.inc();
    connection_manager_.listener_stats_.downstream_rq_3xx_.inc();
  } else if (CodeUtility::is4xx(response_code)) {
    connection_manager_.stats_.named_.downstream_rq_4xx_.inc();
    connection_manager_.listener_stats_.downstream_rq_4xx_.inc();
  } else if (CodeUtility::is5xx(response_code)) {
    connection_manager_.stats_.named_.downstream_rq_5xx_.inc();
    connection_manager_.listener_stats_.downstream_rq_5xx_.inc();
  }
}

const Network::Connection* ConnectionManagerImpl::ActiveStream::connection() {
  return &connection_manager_.read_callbacks_->connection();
}

uint32_t ConnectionManagerImpl::ActiveStream::localPort() {
  auto ip = connection()->localAddress()->ip();
  if (ip == nullptr) {
    return 0;
  }
  return ip->port();
}

// Ordering in this function is complicated, but important.
//
// We want to do minimal work before selecting route and creating a filter
// chain to maximize the number of requests which get custom filter behavior,
// e.g. registering access logging.
//
// This must be balanced by doing sanity checking for invalid requests (one
// can't route select properly without full headers), checking state required to
// serve error responses (connection close, head requests, etc), and
// modifications which may themselves affect route selection.
void ConnectionManagerImpl::ActiveStream::decodeHeaders(RequestHeaderMapPtr&& headers,
                                                        bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  filter_manager_.setRequestHeaders(std::move(headers));
  Upstream::HostDescriptionConstSharedPtr upstream_host =
      connection_manager_.read_callbacks_->upstreamHost();

  if (upstream_host != nullptr) {
    Upstream::ClusterRequestResponseSizeStatsOptRef req_resp_stats =
        upstream_host->cluster().requestResponseSizeStats();
    if (req_resp_stats.has_value()) {
      req_resp_stats->get().upstream_rq_headers_size_.recordValue(
          filter_manager_.requestHeaders()->byteSize());
    }
  }

  // Both saw_connection_close_ and is_head_request_ affect local replies: set
  // them as early as possible.
  const Protocol protocol = connection_manager_.codec_->protocol();
  const bool fixed_connection_close =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.fixed_connection_close");
  if (fixed_connection_close) {
    state_.saw_connection_close_ =
        HeaderUtility::shouldCloseConnection(protocol, *filter_manager_.requestHeaders());
  }
  if (HeaderUtility::isConnect(*filter_manager_.requestHeaders()) &&
      !filter_manager_.requestHeaders()->Path() &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.stop_faking_paths")) {
    filter_manager_.requestHeaders()->setPath("/");
  }

  // We need to snap snapped_route_config_ here as it's used in mutateRequestHeaders later.
  if (connection_manager_.config_.isRoutable()) {
    if (connection_manager_.config_.routeConfigProvider() != nullptr) {
      snapped_route_config_ = connection_manager_.config_.routeConfigProvider()->config();
    } else if (connection_manager_.config_.scopedRouteConfigProvider() != nullptr) {
      snapped_scoped_routes_config_ =
          connection_manager_.config_.scopedRouteConfigProvider()->config<Router::ScopedConfig>();
      snapScopedRouteConfig();
    }
  } else {
    snapped_route_config_ = connection_manager_.config_.routeConfigProvider()->config();
  }

  ENVOY_STREAM_LOG(debug, "request headers complete (end_stream={}):\n{}", *this, end_stream,
                   *filter_manager_.requestHeaders());

  // We end the decode here only if the request is header only. If we convert the request to a
  // header only, the stream will be marked as done once a subsequent decodeData/decodeTrailers is
  // called with end_stream=true.
  filter_manager_.maybeEndDecode(end_stream);

  // Drop new requests when overloaded as soon as we have decoded the headers.
  if (connection_manager_.overload_stop_accepting_requests_ref_.isSaturated()) {
    // In this one special case, do not create the filter chain. If there is a risk of memory
    // overload it is more important to avoid unnecessary allocation than to create the filters.
    filter_manager_.skipFilterChainCreation();
    connection_manager_.stats_.named_.downstream_rq_overload_close_.inc();
    sendLocalReply(Grpc::Common::isGrpcRequestHeaders(*filter_manager_.requestHeaders()),
                   Http::Code::ServiceUnavailable, "envoy overloaded", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().Overload);
    return;
  }

  if (!connection_manager_.config_.proxy100Continue() &&
      filter_manager_.requestHeaders()->Expect() &&
      filter_manager_.requestHeaders()->Expect()->value() ==
          Headers::get().ExpectValues._100Continue.c_str()) {
    // Note in the case Envoy is handling 100-Continue complexity, it skips the filter chain
    // and sends the 100-Continue directly to the encoder.
    chargeStats(continueHeader());
    response_encoder_->encode100ContinueHeaders(continueHeader());
    // Remove the Expect header so it won't be handled again upstream.
    filter_manager_.requestHeaders()->removeExpect();
  }

  connection_manager_.user_agent_.initializeFromHeaders(*filter_manager_.requestHeaders(),
                                                        connection_manager_.stats_.prefixStatName(),
                                                        connection_manager_.stats_.scope_);

  // Make sure we are getting a codec version we support.
  if (protocol == Protocol::Http10) {
    // Assume this is HTTP/1.0. This is fine for HTTP/0.9 but this code will also affect any
    // requests with non-standard version numbers (0.9, 1.3), basically anything which is not
    // HTTP/1.1.
    //
    // The protocol may have shifted in the HTTP/1.0 case so reset it.
    filter_manager_.streamInfo().protocol(protocol);
    if (!connection_manager_.config_.http1Settings().accept_http_10_) {
      // Send "Upgrade Required" if HTTP/1.0 support is not explicitly configured on.
      sendLocalReply(false, Code::UpgradeRequired, "", nullptr, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().LowVersion);
      return;
    } else if (!fixed_connection_close) {
      // HTTP/1.0 defaults to single-use connections. Make sure the connection
      // will be closed unless Keep-Alive is present.
      state_.saw_connection_close_ = true;
      if (absl::EqualsIgnoreCase(filter_manager_.requestHeaders()->getConnectionValue(),
                                 Http::Headers::get().ConnectionValues.KeepAlive)) {
        state_.saw_connection_close_ = false;
      }
    }
    if (!filter_manager_.requestHeaders()->Host() &&
        !connection_manager_.config_.http1Settings().default_host_for_http_10_.empty()) {
      // Add a default host if configured to do so.
      filter_manager_.requestHeaders()->setHost(
          connection_manager_.config_.http1Settings().default_host_for_http_10_);
    }
  }

  if (!filter_manager_.requestHeaders()->Host()) {
    // Require host header. For HTTP/1.1 Host has already been translated to :authority.
    sendLocalReply(Grpc::Common::hasGrpcContentType(*filter_manager_.requestHeaders()),
                   Code::BadRequest, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().MissingHost);
    return;
  }

  // Verify header sanity checks which should have been performed by the codec.
  ASSERT(HeaderUtility::requestHeadersValid(*filter_manager_.requestHeaders()).has_value() ==
         false);

  // Check for the existence of the :path header for non-CONNECT requests, or present-but-empty
  // :path header for CONNECT requests. We expect the codec to have broken the path into pieces if
  // applicable. NOTE: Currently the HTTP/1.1 codec only does this when the allow_absolute_url flag
  // is enabled on the HCM.
  if ((!HeaderUtility::isConnect(*filter_manager_.requestHeaders()) ||
       filter_manager_.requestHeaders()->Path()) &&
      filter_manager_.requestHeaders()->getPathValue().empty()) {
    sendLocalReply(Grpc::Common::hasGrpcContentType(*filter_manager_.requestHeaders()),
                   Code::NotFound, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().MissingPath);
    return;
  }

  // Currently we only support relative paths at the application layer.
  if (!filter_manager_.requestHeaders()->getPathValue().empty() &&
      filter_manager_.requestHeaders()->getPathValue()[0] != '/') {
    connection_manager_.stats_.named_.downstream_rq_non_relative_path_.inc();
    sendLocalReply(Grpc::Common::hasGrpcContentType(*filter_manager_.requestHeaders()),
                   Code::NotFound, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().AbsolutePath);
    return;
  }

  // Path sanitization should happen before any path access other than the above sanity check.
  if (!ConnectionManagerUtility::maybeNormalizePath(*filter_manager_.requestHeaders(),
                                                    connection_manager_.config_)) {
    sendLocalReply(Grpc::Common::hasGrpcContentType(*filter_manager_.requestHeaders()),
                   Code::BadRequest, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().PathNormalizationFailed);
    return;
  }

  ConnectionManagerUtility::maybeNormalizeHost(*filter_manager_.requestHeaders(),
                                               connection_manager_.config_, localPort());

  if (!fixed_connection_close && protocol == Protocol::Http11 &&
      absl::EqualsIgnoreCase(filter_manager_.requestHeaders()->getConnectionValue(),
                             Http::Headers::get().ConnectionValues.Close)) {
    state_.saw_connection_close_ = true;
  }
  // Note: Proxy-Connection is not a standard header, but is supported here
  // since it is supported by http-parser the underlying parser for http
  // requests.
  if (!fixed_connection_close && protocol < Protocol::Http2 && !state_.saw_connection_close_ &&
      absl::EqualsIgnoreCase(filter_manager_.requestHeaders()->getProxyConnectionValue(),
                             Http::Headers::get().ConnectionValues.Close)) {
    state_.saw_connection_close_ = true;
  }

  if (!state_.is_internally_created_) { // Only sanitize headers on first pass.
    // Modify the downstream remote address depending on configuration and headers.
    filter_manager_.streamInfo().setDownstreamRemoteAddress(
        ConnectionManagerUtility::mutateRequestHeaders(
            *filter_manager_.requestHeaders(), connection_manager_.read_callbacks_->connection(),
            connection_manager_.config_, *snapped_route_config_, connection_manager_.local_info_));
  }
  ASSERT(filter_manager_.streamInfo().downstreamRemoteAddress() != nullptr);

  ASSERT(!cached_route_);
  refreshCachedRoute();

  if (!state_.is_internally_created_) { // Only mutate tracing headers on first pass.
    ConnectionManagerUtility::mutateTracingRequestHeader(
        *filter_manager_.requestHeaders(), connection_manager_.runtime_,
        connection_manager_.config_, cached_route_.value().get());
  }

  filter_manager_.streamInfo().setRequestHeaders(*filter_manager_.requestHeaders());

  const bool upgrade_rejected = filter_manager_.createFilterChain() == false;

  // TODO if there are no filters when starting a filter iteration, the connection manager
  // should return 404. The current returns no response if there is no router filter.
  if (hasCachedRoute()) {
    // Do not allow upgrades if the route does not support it.
    if (upgrade_rejected) {
      // While downstream servers should not send upgrade payload without the upgrade being
      // accepted, err on the side of caution and refuse to process any further requests on this
      // connection, to avoid a class of HTTP/1.1 smuggling bugs where Upgrade or CONNECT payload
      // contains a smuggled HTTP request.
      state_.saw_connection_close_ = true;
      connection_manager_.stats_.named_.downstream_rq_ws_on_non_ws_route_.inc();
      sendLocalReply(Grpc::Common::hasGrpcContentType(*filter_manager_.requestHeaders()),
                     Code::Forbidden, "", nullptr, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().UpgradeFailed);
      return;
    }
    // Allow non websocket requests to go through websocket enabled routes.
  }

  if (hasCachedRoute()) {
    const Router::RouteEntry* route_entry = cached_route_.value()->routeEntry();
    if (route_entry != nullptr && route_entry->idleTimeout()) {
      // TODO(mattklein123): Technically if the cached route changes, we should also see if the
      // route idle timeout has changed and update the value.
      idle_timeout_ms_ = route_entry->idleTimeout().value();
      response_encoder_->getStream().setFlushTimeout(idle_timeout_ms_);
      if (idle_timeout_ms_.count()) {
        // If we have a route-level idle timeout but no global stream idle timeout, create a timer.
        if (stream_idle_timer_ == nullptr) {
          stream_idle_timer_ =
              connection_manager_.read_callbacks_->connection().dispatcher().createTimer(
                  [this]() -> void { onIdleTimeout(); });
        }
      } else if (stream_idle_timer_ != nullptr) {
        // If we had a global stream idle timeout but the route-level idle timeout is set to zero
        // (to override), we disable the idle timer.
        stream_idle_timer_->disableTimer();
        stream_idle_timer_ = nullptr;
      }
    }
  }

  // Check if tracing is enabled at all.
  if (connection_manager_.config_.tracingConfig()) {
    traceRequest();
  }

  filter_manager_.decodeHeaders(*filter_manager_.requestHeaders(), end_stream);

  // Reset it here for both global and overridden cases.
  resetIdleTimer();
}

void ConnectionManagerImpl::ActiveStream::traceRequest() {
  Tracing::Decision tracing_decision = Tracing::HttpTracerUtility::isTracing(
      filter_manager_.streamInfo(), *filter_manager_.requestHeaders());
  ConnectionManagerImpl::chargeTracingStats(tracing_decision.reason,
                                            connection_manager_.config_.tracingStats());

  active_span_ = connection_manager_.tracer().startSpan(
      *this, *filter_manager_.requestHeaders(), filter_manager_.streamInfo(), tracing_decision);

  if (!active_span_) {
    return;
  }

  // TODO: Need to investigate the following code based on the cached route, as may
  // be broken in the case a filter changes the route.

  // If a decorator has been defined, apply it to the active span.
  if (hasCachedRoute() && cached_route_.value()->decorator()) {
    const Router::Decorator* decorator = cached_route_.value()->decorator();

    decorator->apply(*active_span_);

    state_.decorated_propagate_ = decorator->propagate();

    // Cache decorated operation.
    if (!decorator->getOperation().empty()) {
      decorated_operation_ = &decorator->getOperation();
    }
  }

  if (connection_manager_.config_.tracingConfig()->operation_name_ ==
      Tracing::OperationName::Egress) {
    // For egress (outbound) requests, pass the decorator's operation name (if defined and
    // propagation enabled) as a request header to enable the receiving service to use it in its
    // server span.
    if (decorated_operation_ && state_.decorated_propagate_) {
      filter_manager_.requestHeaders()->setEnvoyDecoratorOperation(*decorated_operation_);
    }
  } else {
    const HeaderEntry* req_operation_override =
        filter_manager_.requestHeaders()->EnvoyDecoratorOperation();

    // For ingress (inbound) requests, if a decorator operation name has been provided, it
    // should be used to override the active span's operation.
    if (req_operation_override) {
      if (!req_operation_override->value().empty()) {
        active_span_->setOperation(req_operation_override->value().getStringView());

        // Clear the decorated operation so won't be used in the response header, as
        // it has been overridden by the inbound decorator operation request header.
        decorated_operation_ = nullptr;
      }
      // Remove header so not propagated to service
      filter_manager_.requestHeaders()->removeEnvoyDecoratorOperation();
    }
  }
}

void ConnectionManagerImpl::ActiveStream::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  filter_manager_.maybeEndDecode(end_stream);
  filter_manager_.streamInfo().addBytesReceived(data.length());

  filter_manager_.decodeData(data, end_stream);
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(RequestTrailerMapPtr&& trailers) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  resetIdleTimer();
  filter_manager_.maybeEndDecode(true);
  filter_manager_.decodeTrailers(std::move(trailers));
}

void ConnectionManagerImpl::ActiveStream::decodeMetadata(MetadataMapPtr&& metadata_map) {
  resetIdleTimer();
  // After going through filters, the ownership of metadata_map will be passed to terminal filter.
  // The terminal filter may encode metadata_map to the next hop immediately or store metadata_map
  // and encode later when connection pool is ready.
  filter_manager_.decodeMetadata(*metadata_map);
}

void ConnectionManagerImpl::ActiveStream::disarmRequestTimeout() {
  if (request_timer_) {
    request_timer_->disableTimer();
  }
}

void ConnectionManagerImpl::startDrainSequence() {
  ASSERT(drain_state_ == DrainState::NotDraining);
  drain_state_ = DrainState::Draining;
  codec_->shutdownNotice();
  drain_timer_ = read_callbacks_->connection().dispatcher().createTimer(
      [this]() -> void { onDrainTimeout(); });
  drain_timer_->enableTimer(config_.drainTimeout());
}

void ConnectionManagerImpl::ActiveStream::snapScopedRouteConfig() {
  // NOTE: if a RDS subscription hasn't got a RouteConfiguration back, a Router::NullConfigImpl is
  // returned, in that case we let it pass.
  snapped_route_config_ =
      snapped_scoped_routes_config_->getRouteConfig(*filter_manager_.requestHeaders());
  if (snapped_route_config_ == nullptr) {
    ENVOY_STREAM_LOG(trace, "can't find SRDS scope.", *this);
    // TODO(stevenzzzz): Consider to pass an error message to router filter, so that it can
    // send back 404 with some more details.
    snapped_route_config_ = std::make_shared<Router::NullConfigImpl>();
  }
}

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute() { refreshCachedRoute(nullptr); }

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute(const Router::RouteCallback& cb) {
  Router::RouteConstSharedPtr route;
  if (filter_manager_.requestHeaders() != nullptr) {
    if (connection_manager_.config_.isRoutable() &&
        connection_manager_.config_.scopedRouteConfigProvider() != nullptr) {
      // NOTE: re-select scope as well in case the scope key header has been changed by a filter.
      snapScopedRouteConfig();
    }
    if (snapped_route_config_ != nullptr) {
      route = snapped_route_config_->route(cb, *filter_manager_.requestHeaders(),
                                           filter_manager_.streamInfo(), stream_id_);
    }
  }
  filter_manager_.streamInfo().route_entry_ = route ? route->routeEntry() : nullptr;
  cached_route_ = std::move(route);
  if (nullptr == filter_manager_.streamInfo().route_entry_) {
    cached_cluster_info_ = nullptr;
  } else {
    Upstream::ThreadLocalCluster* local_cluster = connection_manager_.cluster_manager_.get(
        filter_manager_.streamInfo().route_entry_->clusterName());
    cached_cluster_info_ = (nullptr == local_cluster) ? nullptr : local_cluster->info();
  }

  filter_manager_.streamInfo().setUpstreamClusterInfo(cached_cluster_info_.value());
  refreshCachedTracingCustomTags();
}

void ConnectionManagerImpl::ActiveStream::refreshCachedTracingCustomTags() {
  if (!connection_manager_.config_.tracingConfig()) {
    return;
  }
  const Tracing::CustomTagMap& conn_manager_tags =
      connection_manager_.config_.tracingConfig()->custom_tags_;
  const Tracing::CustomTagMap* route_tags = nullptr;
  if (hasCachedRoute() && cached_route_.value()->tracingConfig()) {
    route_tags = &cached_route_.value()->tracingConfig()->getCustomTags();
  }
  const bool configured_in_conn = !conn_manager_tags.empty();
  const bool configured_in_route = route_tags && !route_tags->empty();
  if (!configured_in_conn && !configured_in_route) {
    return;
  }
  Tracing::CustomTagMap& custom_tag_map = getOrMakeTracingCustomTagMap();
  if (configured_in_route) {
    custom_tag_map.insert(route_tags->begin(), route_tags->end());
  }
  if (configured_in_conn) {
    custom_tag_map.insert(conn_manager_tags.begin(), conn_manager_tags.end());
  }
}

void ConnectionManagerImpl::ActiveStream::requestRouteConfigUpdate(
    Event::Dispatcher& thread_local_dispatcher,
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  ASSERT(!filter_manager_.requestHeaders()->Host()->value().empty());
  const auto& host_header = absl::AsciiStrToLower(filter_manager_.requestHeaders()->getHostValue());
  route_config_update_requester_->requestRouteConfigUpdate(host_header, thread_local_dispatcher,
                                                           std::move(route_config_updated_cb));
}

absl::optional<Router::ConfigConstSharedPtr> ConnectionManagerImpl::ActiveStream::routeConfig() {
  if (connection_manager_.config_.routeConfigProvider() == nullptr) {
    return {};
  }
  return absl::optional<Router::ConfigConstSharedPtr>(
      connection_manager_.config_.routeConfigProvider()->config());
}

void ConnectionManagerImpl::ActiveStream::onLocalReply(Code code) {
  // The BadRequest error code indicates there has been a messaging error.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.hcm_stream_error_on_invalid_message") &&
      !connection_manager_.config_.streamErrorOnInvalidHttpMessaging() &&
      code == Http::Code::BadRequest && connection_manager_.codec_->protocol() < Protocol::Http2) {
    state_.saw_connection_close_ = true;
  }
}

void ConnectionManagerImpl::ActiveStream::encode100ContinueHeaders(
    ResponseHeaderMap& response_headers) {
  // Strip the T-E headers etc. Defer other header additions as well as drain-close logic to the
  // continuation headers.
  ConnectionManagerUtility::mutateResponseHeaders(response_headers,
                                                  filter_manager_.requestHeaders(),
                                                  connection_manager_.config_, EMPTY_STRING);

  // Count both the 1xx and follow-up response code in stats.
  chargeStats(response_headers);

  ENVOY_STREAM_LOG(debug, "encoding 100 continue headers via codec:\n{}", *this, response_headers);

  // Now actually encode via the codec.
  response_encoder_->encode100ContinueHeaders(response_headers);
}

void ConnectionManagerImpl::ActiveStream::encodeHeaders(ResponseHeaderMap& headers,
                                                        bool end_stream) {
  // Base headers.

  // By default, always preserve the upstream date response header if present. If we choose to
  // overwrite the upstream date unconditionally (a previous behavior), only do so if the response
  // is not from cache
  const bool should_preserve_upstream_date =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.preserve_upstream_date") ||
      filter_manager_.streamInfo().hasResponseFlag(
          StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  if (!should_preserve_upstream_date || !headers.Date()) {
    connection_manager_.config_.dateProvider().setDateHeader(headers);
  }

  // Following setReference() is safe because serverName() is constant for the life of the listener.
  const auto transformation = connection_manager_.config_.serverHeaderTransformation();
  if (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE ||
      (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::APPEND_IF_ABSENT &&
       headers.Server() == nullptr)) {
    headers.setReferenceServer(connection_manager_.config_.serverName());
  }
  ConnectionManagerUtility::mutateResponseHeaders(headers, filter_manager_.requestHeaders(),
                                                  connection_manager_.config_,
                                                  connection_manager_.config_.via());

  bool drain_connection_due_to_overload = false;
  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.overload_disable_keepalive_ref_.isSaturated()) {
    ENVOY_STREAM_LOG(debug, "disabling keepalive due to envoy overload", *this);
    if (connection_manager_.codec_->protocol() < Protocol::Http2 ||
        Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2")) {
      drain_connection_due_to_overload = true;
    }
    connection_manager_.stats_.named_.downstream_cx_overload_disable_keepalive_.inc();
  }

  // See if we want to drain/close the connection. Send the go away frame prior to encoding the
  // header block.
  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      (connection_manager_.drain_close_.drainClose() || drain_connection_due_to_overload)) {

    // This doesn't really do anything for HTTP/1.1 other then give the connection another boost
    // of time to race with incoming requests. For HTTP/2 connections, send a GOAWAY frame to
    // prevent any new streams.
    connection_manager_.startDrainSequence();
    connection_manager_.stats_.named_.downstream_cx_drain_close_.inc();
    ENVOY_STREAM_LOG(debug, "drain closing connection", *this);
  }

  if (connection_manager_.codec_->protocol() == Protocol::Http10) {
    // As HTTP/1.0 and below can not do chunked encoding, if there is no content
    // length the response will be framed by connection close.
    if (!headers.ContentLength()) {
      state_.saw_connection_close_ = true;
    }
    // If the request came with a keep-alive and no other factor resulted in a
    // connection close header, send an explicit keep-alive header.
    if (!state_.saw_connection_close_) {
      headers.setConnection(Headers::get().ConnectionValues.KeepAlive);
    }
  }

  if (connection_manager_.drain_state_ == DrainState::NotDraining && state_.saw_connection_close_) {
    ENVOY_STREAM_LOG(debug, "closing connection due to connection close header", *this);
    connection_manager_.drain_state_ = DrainState::Closing;
  }

  // If we are destroying a stream before remote is complete and the connection does not support
  // multiplexing, we should disconnect since we don't want to wait around for the request to
  // finish.
  if (!filter_manager_.remoteComplete()) {
    if (connection_manager_.codec_->protocol() < Protocol::Http2) {
      connection_manager_.drain_state_ = DrainState::Closing;
    }

    connection_manager_.stats_.named_.downstream_rq_response_before_rq_complete_.inc();
  }

  if (connection_manager_.drain_state_ != DrainState::NotDraining &&
      connection_manager_.codec_->protocol() < Protocol::Http2) {
    // If the connection manager is draining send "Connection: Close" on HTTP/1.1 connections.
    // Do not do this for H2 (which drains via GOAWAY) or Upgrade or CONNECT (as the
    // payload is no longer HTTP/1.1)
    if (!Utility::isUpgrade(headers) &&
        !HeaderUtility::isConnectResponse(filter_manager_.requestHeaders(),
                                          *filter_manager_.responseHeaders())) {
      headers.setReferenceConnection(Headers::get().ConnectionValues.Close);
    }
  }

  if (connection_manager_.config_.tracingConfig()) {
    if (connection_manager_.config_.tracingConfig()->operation_name_ ==
        Tracing::OperationName::Ingress) {
      // For ingress (inbound) responses, if the request headers do not include a
      // decorator operation (override), and the decorated operation should be
      // propagated, then pass the decorator's operation name (if defined)
      // as a response header to enable the client service to use it in its client span.
      if (decorated_operation_ && state_.decorated_propagate_) {
        headers.setEnvoyDecoratorOperation(*decorated_operation_);
      }
    } else if (connection_manager_.config_.tracingConfig()->operation_name_ ==
               Tracing::OperationName::Egress) {
      const HeaderEntry* resp_operation_override = headers.EnvoyDecoratorOperation();

      // For Egress (outbound) response, if a decorator operation name has been provided, it
      // should be used to override the active span's operation.
      if (resp_operation_override) {
        if (!resp_operation_override->value().empty() && active_span_) {
          active_span_->setOperation(resp_operation_override->value().getStringView());
        }
        // Remove header so not propagated to service.
        headers.removeEnvoyDecoratorOperation();
      }
    }
  }

  chargeStats(headers);

  ENVOY_STREAM_LOG(debug, "encoding headers via codec (end_stream={}):\n{}", *this, end_stream,
                   headers);

  // Now actually encode via the codec.
  filter_manager_.streamInfo().onFirstDownstreamTxByteSent();
  response_encoder_->encodeHeaders(headers, end_stream);
}

void ConnectionManagerImpl::ActiveStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(trace, "encoding data via codec (size={} end_stream={})", *this, data.length(),
                   end_stream);

  filter_manager_.streamInfo().addBytesSent(data.length());
  response_encoder_->encodeData(data, end_stream);
}

void ConnectionManagerImpl::ActiveStream::encodeTrailers(ResponseTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "encoding trailers via codec:\n{}", *this, trailers);

  response_encoder_->encodeTrailers(trailers);
}

void ConnectionManagerImpl::ActiveStream::encodeMetadata(MetadataMapVector& metadata) {
  ENVOY_STREAM_LOG(debug, "encoding metadata via codec:\n{}", *this, metadata);
  response_encoder_->encodeMetadata(metadata);
}

void ConnectionManagerImpl::ActiveStream::onDecoderFilterBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Read-enabling downstream stream due to filter callbacks.", *this);
  // If the state is destroyed, the codec's stream is already torn down. On
  // teardown the codec will unwind any remaining read disable calls.
  if (!filter_manager_.destroyed()) {
    response_encoder_->getStream().readDisable(false);
  }
  connection_manager_.stats_.named_.downstream_flow_control_resumed_reading_total_.inc();
}

void ConnectionManagerImpl::ActiveStream::onDecoderFilterAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Read-disabling downstream stream due to filter callbacks.", *this);
  response_encoder_->getStream().readDisable(true);
  connection_manager_.stats_.named_.downstream_flow_control_paused_reading_total_.inc();
}

void ConnectionManagerImpl::ActiveStream::onResetStream(StreamResetReason, absl::string_view) {
  // NOTE: This function gets called in all of the following cases:
  //       1) We TX an app level reset
  //       2) The codec TX a codec level reset
  //       3) The codec RX a reset
  //       If we need to differentiate we need to do it inside the codec. Can start with this.
  ENVOY_STREAM_LOG(debug, "stream reset", *this);
  connection_manager_.stats_.named_.downstream_rq_rx_reset_.inc();

  // If the codec sets its responseDetails(), impute a
  // DownstreamProtocolError and propagate the details upwards.
  const absl::string_view encoder_details = response_encoder_->getStream().responseDetails();
  if (!encoder_details.empty()) {
    filter_manager_.streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError);
    filter_manager_.streamInfo().setResponseCodeDetails(encoder_details);
  }

  connection_manager_.doDeferredStreamDestroy(*this);
}

void ConnectionManagerImpl::ActiveStream::onAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Disabling upstream stream due to downstream stream watermark.", *this);
  filter_manager_.callHighWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStream::onBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Enabling upstream stream due to downstream stream watermark.", *this);
  filter_manager_.callLowWatermarkCallbacks();
}

Tracing::OperationName ConnectionManagerImpl::ActiveStream::operationName() const {
  return connection_manager_.config_.tracingConfig()->operation_name_;
}

const Tracing::CustomTagMap* ConnectionManagerImpl::ActiveStream::customTags() const {
  return tracing_custom_tags_.get();
}

bool ConnectionManagerImpl::ActiveStream::verbose() const {
  return connection_manager_.config_.tracingConfig()->verbose_;
}

uint32_t ConnectionManagerImpl::ActiveStream::maxPathTagLength() const {
  return connection_manager_.config_.tracingConfig()->max_path_tag_length_;
}

const Router::RouteEntry::UpgradeMap* ConnectionManagerImpl::ActiveStream::upgradeMap() {
  // We must check if the 'cached_route_' optional is populated since this function can be called
  // early via sendLocalReply(), before the cached route is populated.
  if (hasCachedRoute() && cached_route_.value()->routeEntry()) {
    return &cached_route_.value()->routeEntry()->upgradeMap();
  }

  return nullptr;
}

Tracing::Span& ConnectionManagerImpl::ActiveStream::activeSpan() {
  if (active_span_) {
    return *active_span_;
  } else {
    return Tracing::NullSpan::instance();
  }
}

Tracing::Config& ConnectionManagerImpl::ActiveStream::tracingConfig() { return *this; }

const ScopeTrackedObject& ConnectionManagerImpl::ActiveStream::scope() { return *this; }

Upstream::ClusterInfoConstSharedPtr ConnectionManagerImpl::ActiveStream::clusterInfo() {
  // NOTE: Refreshing route caches clusterInfo as well.
  if (!cached_route_.has_value()) {
    refreshCachedRoute();
  }

  return cached_cluster_info_.value();
}

Router::RouteConstSharedPtr
ConnectionManagerImpl::ActiveStream::route(const Router::RouteCallback& cb) {
  if (cached_route_.has_value()) {
    return cached_route_.value();
  }
  refreshCachedRoute(cb);
  return cached_route_.value();
}

void ConnectionManagerImpl::ActiveStream::clearRouteCache() {
  cached_route_ = absl::optional<Router::RouteConstSharedPtr>();
  cached_cluster_info_ = absl::optional<Upstream::ClusterInfoConstSharedPtr>();
  if (tracing_custom_tags_) {
    tracing_custom_tags_->clear();
  }
}

void ConnectionManagerImpl::ActiveStream::onRequestDataTooLarge() {
  connection_manager_.stats_.named_.downstream_rq_too_large_.inc();
}

void ConnectionManagerImpl::ActiveStream::recreateStream(
    RequestHeaderMapPtr&& request_headers, StreamInfo::FilterStateSharedPtr filter_state) {
  // n.b. we do not currently change the codecs to point at the new stream
  // decoder because the decoder callbacks are complete. It would be good to
  // null out that pointer but should not be necessary.
  ResponseEncoder* response_encoder = response_encoder_;
  response_encoder_ = nullptr;

  response_encoder->getStream().removeCallbacks(*this);
  // This functionally deletes the stream (via deferred delete) so do not
  // reference anything beyond this point.
  connection_manager_.doEndStream(*this);

  RequestDecoder& new_stream = connection_manager_.newStream(*response_encoder, true);
  // We don't need to copy over the old parent FilterState from the old StreamInfo if it did not
  // store any objects with a LifeSpan at or above DownstreamRequest. This is to avoid unnecessary
  // heap allocation.
  // TODO(snowp): In the case where connection level filter state has been set on the connection
  // FilterState that we inherit, we'll end up copying this every time even though we could get
  // away with just resetting it to the HCM filter_state_.
  if (filter_state->hasDataAtOrAboveLifeSpan(StreamInfo::FilterState::LifeSpan::Request)) {
    (*connection_manager_.streams_.begin())->filter_manager_.streamInfo().filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(
            filter_state->parent(), StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  new_stream.decodeHeaders(std::move(request_headers), true);
}

Http1StreamEncoderOptionsOptRef ConnectionManagerImpl::ActiveStream::http1StreamEncoderOptions() {
  return response_encoder_->http1StreamEncoderOptions();
}

void ConnectionManagerImpl::ActiveStream::onResponseDataTooLarge() {
  connection_manager_.stats_.named_.rs_too_large_.inc();
}

void ConnectionManagerImpl::ActiveStream::resetStream() {
  connection_manager_.stats_.named_.downstream_rq_tx_reset_.inc();
  connection_manager_.doEndStream(*this);
}

} // namespace Http
} // namespace Envoy
