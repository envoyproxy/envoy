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

namespace {

template <class T> using FilterList = std::list<std::unique_ptr<T>>;

// Shared helper for recording the latest filter used.
template <class T>
void recordLatestDataFilter(const typename FilterList<T>::iterator current_filter,
                            T*& latest_filter, const FilterList<T>& filters) {
  // If this is the first time we're calling onData, just record the current filter.
  if (latest_filter == nullptr) {
    latest_filter = current_filter->get();
    return;
  }

  // We want to keep this pointing at the latest filter in the filter list that has received the
  // onData callback. To do so, we compare the current latest with the *previous* filter. If they
  // match, then we must be processing a new filter for the first time. We omit this check if we're
  // the first filter, since the above check handles that case.
  //
  // We compare against the previous filter to avoid multiple filter iterations from resetting the
  // pointer: If we just set latest to current, then the first onData filter iteration would
  // correctly iterate over the filters and set latest, but on subsequent onData iterations
  // we'd start from the beginning again, potentially allowing filter N to modify the buffer even
  // though filter M > N was the filter that inserted data into the buffer.
  if (current_filter != filters.begin() && latest_filter == std::prev(current_filter)->get()) {
    latest_filter = current_filter->get();
  }
}

} // namespace

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
    doConnectionClose(Network::ConnectionCloseType::FlushWriteAndDelay, absl::nullopt);
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
  read_callbacks_->connection().streamInfo().setResponseCodeDetails(
      absl::StrCat("codec error: ", error));
  read_callbacks_->connection().streamInfo().setResponseFlag(
      StreamInfo::ResponseFlag::DownstreamProtocolError);

  // HTTP/1.1 codec has already sent a 400 response if possible. HTTP/2 codec has already sent
  // GOAWAY.
  doConnectionClose(Network::ConnectionCloseType::FlushWriteAndDelay,
                    StreamInfo::ResponseFlag::DownstreamProtocolError);
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

void ConnectionManagerImpl::resetAllStreams(
    absl::optional<StreamInfo::ResponseFlag> response_flag) {
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
    stream.onResetStream(StreamResetReason::ConnectionTermination, absl::string_view());
    if (response_flag.has_value()) {
      // This code duplicates some of the logic in
      // onResetStream(). There seems to be no easy way to force
      // onResetStream to do the right thing within its current API.
      // Encoding DownstreamProtocolError as reason==LocalReset does
      // not work because local reset is generated in other places.
      // Encoding it in the string_view argument would lead to a hack
      // of the form: if parameter is nonempty, use that; else if the
      // codec details are nonempty, use those. This hack does not
      // seem better than the code duplication, so punt for now.
      stream.filter_manager_.streamInfo().setResponseFlag(response_flag.value());
      if (*response_flag == StreamInfo::ResponseFlag::DownstreamProtocolError) {
        stream.filter_manager_.streamInfo().setResponseCodeDetails(
            stream.response_encoder_->getStream().responseDetails());
      }
    }
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
    // TODO(mattklein123): It is technically possible that something outside of the filter causes
    // a local connection close, so we still guard against that here. A better solution would be to
    // have some type of "pre-close" callback that we could hook for cleanup that would get called
    // regardless of where local close is invoked from.
    // NOTE: that this will cause doConnectionClose() to get called twice in the common local close
    // cases, but the method protects against that.
    // NOTE: In the case where a local close comes from outside the filter, this will cause any
    // stream closures to increment remote close stats. We should do better here in the future,
    // via the pre-close callback mentioned above.
    doConnectionClose(absl::nullopt, absl::nullopt);
  }
}

void ConnectionManagerImpl::doConnectionClose(
    absl::optional<Network::ConnectionCloseType> close_type,
    absl::optional<StreamInfo::ResponseFlag> response_flag) {
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
    resetAllStreams(response_flag);
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
    doConnectionClose(Network::ConnectionCloseType::FlushWrite, absl::nullopt);
  } else if (drain_state_ == DrainState::NotDraining) {
    startDrainSequence();
  }
}

void ConnectionManagerImpl::onConnectionDurationTimeout() {
  ENVOY_CONN_LOG(debug, "max connection duration reached", read_callbacks_->connection());
  stats_.named_.downstream_cx_max_duration_reached_.inc();
  if (!codec_) {
    // Attempt to write out buffered data one last time and issue a local close if successful.
    doConnectionClose(Network::ConnectionCloseType::FlushWrite, absl::nullopt);
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
      filter_manager_(*this, *this, buffer_limit, connection_manager_.config_.filterFactory(),
                      connection_manager_.config_.localReply(),
                      connection_manager_.codec_->protocol(), connection_manager_.timeSource(),
                      connection_manager_.read_callbacks_->connection().streamInfo().filterState(),
                      StreamInfo::FilterState::LifeSpan::Connection),
      stream_id_(connection_manager.random_generator_.random()),
      request_response_timespan_(new Stats::HistogramCompletableTimespanImpl(
          connection_manager_.stats_.named_.downstream_rq_time_, connection_manager_.timeSource())),
      upstream_options_(std::make_shared<Network::Socket::Options>()) {
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

  // TODO(alyssawilk) this is not true. Fix.
  // A downstream disconnect can be identified for HTTP requests when the upstream returns with a 0
  // response code and when no other response flags are set.
  if (!filter_manager_.streamInfo().hasAnyResponseFlag() &&
      !filter_manager_.streamInfo().responseCode()) {
    filter_manager_.streamInfo().setResponseFlag(
        StreamInfo::ResponseFlag::DownstreamConnectionTermination);
  }
  if (connection_manager_.remote_close_) {
    filter_manager_.streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().DownstreamRemoteDisconnect);
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

void ConnectionManagerImpl::FilterManager::addStreamDecoderFilterWorker(
    StreamDecoderFilterSharedPtr filter, bool dual_filter) {
  ActiveStreamDecoderFilterPtr wrapper(new ActiveStreamDecoderFilter(*this, filter, dual_filter));
  filter->setDecoderFilterCallbacks(*wrapper);
  // Note: configured decoder filters are appended to decoder_filters_.
  // This means that if filters are configured in the following order (assume all three filters are
  // both decoder/encoder filters):
  //   http_filters:
  //     - A
  //     - B
  //     - C
  // The decoder filter chain will iterate through filters A, B, C.
  LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void ConnectionManagerImpl::FilterManager::addStreamEncoderFilterWorker(
    StreamEncoderFilterSharedPtr filter, bool dual_filter) {
  ActiveStreamEncoderFilterPtr wrapper(new ActiveStreamEncoderFilter(*this, filter, dual_filter));
  filter->setEncoderFilterCallbacks(*wrapper);
  // Note: configured encoder filters are prepended to encoder_filters_.
  // This means that if filters are configured in the following order (assume all three filters are
  // both decoder/encoder filters):
  //   http_filters:
  //     - A
  //     - B
  //     - C
  // The encoder filter chain will iterate through filters C, B, A.
  LinkedList::moveIntoList(std::move(wrapper), encoder_filters_);
}

void ConnectionManagerImpl::FilterManager::addAccessLogHandler(
    AccessLog::InstanceSharedPtr handler) {
  access_log_handlers_.push_back(handler);
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
  if (Http::Headers::get().MethodValues.Head ==
      filter_manager_.requestHeaders()->getMethodValue()) {
    state_.is_head_request_ = true;
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

void ConnectionManagerImpl::FilterManager::maybeContinueDecoding(
    const std::list<ActiveStreamDecoderFilterPtr>::iterator& continue_data_entry) {
  if (continue_data_entry != decoder_filters_.end()) {
    // We use the continueDecoding() code since it will correctly handle not calling
    // decodeHeaders() again. Fake setting StopSingleIteration since the continueDecoding() code
    // expects it.
    ASSERT(buffered_request_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueDecoding();
  }
}

void ConnectionManagerImpl::FilterManager::decodeHeaders(ActiveStreamDecoderFilter* filter,
                                                         RequestHeaderMap& headers,
                                                         bool end_stream) {
  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamDecoderFilterPtr>::iterator continue_data_entry = decoder_filters_.end();

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeHeaders));
    state_.filter_call_state_ |= FilterCallState::DecodeHeaders;
    (*entry)->end_stream_ = state_.decoding_headers_only_ ||
                            (end_stream && continue_data_entry == decoder_filters_.end());
    FilterHeadersStatus status = (*entry)->decodeHeaders(headers, (*entry)->end_stream_);

    ASSERT(!(status == FilterHeadersStatus::ContinueAndEndStream && (*entry)->end_stream_));
    state_.filter_call_state_ &= ~FilterCallState::DecodeHeaders;
    ENVOY_STREAM_LOG(trace, "decode headers called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    const bool new_metadata_added = processNewlyAddedMetadata();
    // If end_stream is set in headers, and a filter adds new metadata, we need to delay end_stream
    // in headers by inserting an empty data frame with end_stream set. The empty data frame is sent
    // after the new metadata.
    if ((*entry)->end_stream_ && new_metadata_added && !buffered_request_data_) {
      Buffer::OwnedImpl empty_data("");
      ENVOY_STREAM_LOG(trace,
                       "inserting an empty data frame for end_stream due metadata being added.",
                       active_stream_);
      // Metadata frame doesn't carry end of stream bit. We need an empty data frame to end the
      // stream.
      addDecodedData(*((*entry).get()), empty_data, true);
    }

    (*entry)->decode_headers_called_ = true;
    if (!(*entry)->commonHandleAfterHeadersCallback(status, state_.decoding_headers_only_) &&
        std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added body.
      maybeContinueDecoding(continue_data_entry);
      return;
    }

    // Here we handle the case where we have a header only request, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_request_data_ && continue_data_entry == decoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  maybeContinueDecoding(continue_data_entry);

  if (end_stream) {
    disarmRequestTimeout();
  }
}

void ConnectionManagerImpl::ActiveStream::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  filter_manager_.maybeEndDecode(end_stream);
  filter_manager_.streamInfo().addBytesReceived(data.length());

  filter_manager_.decodeData(data, end_stream);
}

void ConnectionManagerImpl::FilterManager::decodeData(
    ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream,
    FilterIterationStartState filter_iteration_start_state) {
  ScopeTrackerScopeState scope(
      &active_stream_,
      active_stream_.connection_manager_.read_callbacks_->connection().dispatcher());
  active_stream_.resetIdleTimer();

  // If we previously decided to decode only the headers, do nothing here.
  if (state_.decoding_headers_only_) {
    return;
  }

  // If a response is complete or a reset has been sent, filters do not care about further body
  // data. Just drop it.
  if (state_.local_complete_) {
    return;
  }

  auto trailers_added_entry = decoder_filters_.end();
  const bool trailers_exists_at_start = request_trailers_ != nullptr;
  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, filter_iteration_start_state);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame types, return now.
    if (handleDataIfStopAll(**entry, data, state_.decoder_filters_streaming_)) {
      return;
    }
    // If end_stream_ is marked for a filter, the data is not for this filter and filters after.
    //
    // In following case, ActiveStreamFilterBase::commonContinue() could be called recursively and
    // its doData() is called with wrong data.
    //
    //  There are 3 decode filters and "wrapper" refers to ActiveStreamFilter object.
    //
    //  filter0->decodeHeaders(_, true)
    //    return STOP
    //  filter0->continueDecoding()
    //    wrapper0->commonContinue()
    //      wrapper0->decodeHeaders(_, _, true)
    //        filter1->decodeHeaders(_, true)
    //          filter1->addDecodeData()
    //          return CONTINUE
    //        filter2->decodeHeaders(_, false)
    //          return CONTINUE
    //        wrapper1->commonContinue() // Detects data is added.
    //          wrapper1->doData()
    //            wrapper1->decodeData()
    //              filter2->decodeData(_, true)
    //                 return CONTINUE
    //      wrapper0->doData() // This should not be called
    //        wrapper0->decodeData()
    //          filter1->decodeData(_, true)  // It will cause assertions.
    //
    // One way to solve this problem is to mark end_stream_ for each filter.
    // If a filter is already marked as end_stream_ when decodeData() is called, bails out the
    // whole function. If just skip the filter, the codes after the loop will be called with
    // wrong data. For encodeData, the response_encoder->encode() will be called.
    if ((*entry)->end_stream_) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeData));

    // We check the request_trailers_ pointer here in case addDecodedTrailers
    // is called in decodeData during a previous filter invocation, at which point we communicate to
    // the current and future filters that the stream has not yet ended.
    if (end_stream) {
      state_.filter_call_state_ |= FilterCallState::LastDataFrame;
    }

    recordLatestDataFilter(entry, state_.latest_data_decoding_filter_, decoder_filters_);

    state_.filter_call_state_ |= FilterCallState::DecodeData;
    (*entry)->end_stream_ = end_stream && !request_trailers_;
    FilterDataStatus status = (*entry)->handle_->decodeData(data, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->decodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::DecodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "decode data called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    processNewlyAddedMetadata();

    if (!trailers_exists_at_start && request_trailers_ &&
        trailers_added_entry == decoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.decoder_filters_streaming_) &&
        std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added trailers.
      return;
    }
  }

  // If trailers were adding during decodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != decoder_filters_.end()) {
    decodeTrailers(trailers_added_entry->get(), *request_trailers_);
  }

  if (end_stream) {
    disarmRequestTimeout();
  }
}

RequestTrailerMap& ConnectionManagerImpl::FilterManager::addDecodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!request_trailers_);

  request_trailers_ = RequestTrailerMapImpl::create();
  return *request_trailers_;
}

void ConnectionManagerImpl::FilterManager::addDecodedData(ActiveStreamDecoderFilter& filter,
                                                          Buffer::Instance& data, bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::DecodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::DecodeData) ||
      ((state_.filter_call_state_ & FilterCallState::DecodeTrailers) && !filter.canIterate())) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.decoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::DecodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    decodeData(&filter, data, false, FilterIterationStartState::AlwaysStartFromNext);
  } else {
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

MetadataMapVector& ConnectionManagerImpl::FilterManager::addDecodedMetadata() {
  return *getRequestMetadataMapVector();
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(RequestTrailerMapPtr&& trailers) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  resetIdleTimer();
  filter_manager_.maybeEndDecode(true);
  filter_manager_.decodeTrailers(std::move(trailers));
}

void ConnectionManagerImpl::FilterManager::decodeTrailers(ActiveStreamDecoderFilter* filter,
                                                          RequestTrailerMap& trailers) {
  // If we previously decided to decode only the headers, do nothing here.
  if (state_.decoding_headers_only_) {
    return;
  }

  // See decodeData() above for why we check local_complete_ here.
  if (state_.local_complete_) {
    return;
  }

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if ((*entry)->stoppedAll()) {
      return;
    }

    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeTrailers));
    state_.filter_call_state_ |= FilterCallState::DecodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->decodeTrailers(trailers);
    (*entry)->handle_->decodeComplete();
    (*entry)->end_stream_ = true;
    state_.filter_call_state_ &= ~FilterCallState::DecodeTrailers;
    ENVOY_STREAM_LOG(trace, "decode trailers called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    processNewlyAddedMetadata();

    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }
  disarmRequestTimeout();
}

void ConnectionManagerImpl::ActiveStream::decodeMetadata(MetadataMapPtr&& metadata_map) {
  resetIdleTimer();
  // After going through filters, the ownership of metadata_map will be passed to terminal filter.
  // The terminal filter may encode metadata_map to the next hop immediately or store metadata_map
  // and encode later when connection pool is ready.
  filter_manager_.decodeMetadata(*metadata_map);
}

void ConnectionManagerImpl::FilterManager::decodeMetadata(ActiveStreamDecoderFilter* filter,
                                                          MetadataMap& metadata_map) {
  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, stores metadata and returns.
    // If the filter pointed by entry hasn't returned from decodeHeaders, stores newly added
    // metadata in case decodeHeaders returns StopAllIteration. The latter can happen when headers
    // callbacks generate new metadata.
    if (!(*entry)->decode_headers_called_ || (*entry)->stoppedAll()) {
      Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
      (*entry)->getSavedRequestMetadata()->emplace_back(std::move(metadata_map_ptr));
      return;
    }

    FilterMetadataStatus status = (*entry)->handle_->decodeMetadata(metadata_map);
    ENVOY_STREAM_LOG(trace, "decode metadata called: filter={} status={}, metadata: {}",
                     active_stream_, static_cast<const void*>((*entry).get()),
                     static_cast<uint64_t>(status), metadata_map);
  }
}

void ConnectionManagerImpl::FilterManager::maybeEndDecode(bool end_stream) {
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;
  if (end_stream) {
    stream_info_.onLastDownstreamRxByteReceived();
    ENVOY_STREAM_LOG(debug, "request end stream", active_stream_);
  }
}

void ConnectionManagerImpl::FilterManager::disarmRequestTimeout() {
  if (active_stream_.request_timer_) {
    active_stream_.request_timer_->disableTimer();
  }
}

std::list<ConnectionManagerImpl::ActiveStreamEncoderFilterPtr>::iterator
ConnectionManagerImpl::FilterManager::commonEncodePrefix(
    ActiveStreamEncoderFilter* filter, bool end_stream,
    FilterIterationStartState filter_iteration_start_state) {
  // Only do base state setting on the initial call. Subsequent calls for filtering do not touch
  // the base state.
  if (filter == nullptr) {
    ASSERT(!state_.local_complete_);
    state_.local_complete_ = end_stream;
    return encoder_filters_.begin();
  }

  if (filter_iteration_start_state == FilterIterationStartState::CanStartFromCurrent &&
      (*(filter->entry()))->iterate_from_current_filter_) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's encoding callback has not be called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
}

std::list<ConnectionManagerImpl::ActiveStreamDecoderFilterPtr>::iterator
ConnectionManagerImpl::FilterManager::commonDecodePrefix(
    ActiveStreamDecoderFilter* filter, FilterIterationStartState filter_iteration_start_state) {
  if (!filter) {
    return decoder_filters_.begin();
  }
  if (filter_iteration_start_state == FilterIterationStartState::CanStartFromCurrent &&
      (*(filter->entry()))->iterate_from_current_filter_) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's callback function has not been called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
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

void ConnectionManagerImpl::ActiveStream::sendLocalReply(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  const bool is_head_request = state_.is_head_request_;
  filter_manager_.streamInfo().setResponseCodeDetails(details);

  // The BadRequest error code indicates there has been a messaging error.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.hcm_stream_error_on_invalid_message") &&
      !connection_manager_.config_.streamErrorOnInvalidHttpMessaging() &&
      code == Http::Code::BadRequest && connection_manager_.codec_->protocol() < Protocol::Http2) {
    state_.saw_connection_close_ = true;
  }

  if (filter_manager_.responseHeaders() == nullptr) {
    // If the response has not started at all, send the response through the filter chain.
    filter_manager_.sendLocalReplyViaFilterChain(is_grpc_request, code, body, modify_headers,
                                                 is_head_request, grpc_status, details);
  } else if (!state_.non_100_response_headers_encoded_) {
    ENVOY_STREAM_LOG(debug, "Sending local reply with details {} directly to the encoder", *this,
                     details);
    // In this case, at least the header and possibly the body has started
    // processing through the filter chain, but no non-informational headers
    // have been sent downstream. To ensure that filters don't get their
    // state machine screwed up, bypass the filter chain and send the local
    // reply directly to the codec.
    //
    filter_manager_.sendDirectLocalReply(code, body, modify_headers, state_.is_head_request_,
                                         grpc_status);
  } else {
    filter_manager_.streamInfo().setResponseCodeDetails(details);
    // If we land in this branch, response headers have already been sent to the client.
    // All we can do at this point is reset the stream.
    ENVOY_STREAM_LOG(debug, "Resetting stream due to {}. Prior headers have already been sent",
                     *this, details);
    connection_manager_.doEndStream(*this);
  }
}

void ConnectionManagerImpl::FilterManager::sendLocalReplyViaFilterChain(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  ENVOY_STREAM_LOG(debug, "Sending local reply with details {}", active_stream_, details);
  ASSERT(response_headers_ == nullptr);
  // For early error handling, do a best-effort attempt to create a filter chain
  // to ensure access logging. If the filter chain already exists this will be
  // a no-op.
  createFilterChain();

  Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          [this](ResponseHeaderMap& response_headers, Code& code, std::string& body,
                 absl::string_view& content_type) -> void {
            active_stream_.connection_manager_.config_.localReply().rewrite(
                request_headers_.get(), response_headers, stream_info_, code, body, content_type);
          },
          [this, modify_headers](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
            if (modify_headers != nullptr) {
              modify_headers(*headers);
            }
            response_headers_ = std::move(headers);
            // TODO: Start encoding from the last decoder filter that saw the
            // request instead.
            encodeHeaders(nullptr, *response_headers_, end_stream);
          },
          [this](Buffer::Instance& data, bool end_stream) -> void {
            // TODO: Start encoding from the last decoder filter that saw the
            // request instead.
            encodeData(nullptr, data, end_stream,
                       FilterManager::FilterIterationStartState::CanStartFromCurrent);
          }},
      Utility::LocalReplyData{is_grpc_request, code, body, grpc_status, is_head_request});
}

void ConnectionManagerImpl::FilterManager::sendDirectLocalReply(
    Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap&)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status) {
  // Make sure we won't end up with nested watermark calls from the body buffer.
  state_.encoder_filters_streaming_ = true;
  Http::Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          [&](ResponseHeaderMap& response_headers, Code& code, std::string& body,
              absl::string_view& content_type) -> void {
            local_reply_.rewrite(request_headers_.get(), response_headers, stream_info_, code, body,
                                 content_type);
          },
          [&](ResponseHeaderMapPtr&& response_headers, bool end_stream) -> void {
            if (modify_headers != nullptr) {
              modify_headers(*response_headers);
            }

            // Move the response headers into the FilterManager to make sure they're visible to
            // access logs.
            response_headers_ = std::move(response_headers);
            filter_manager_callbacks_.encodeHeaders(*response_headers_, end_stream);
            maybeEndEncode(end_stream);
          },
          [&](Buffer::Instance& data, bool end_stream) -> void {
            filter_manager_callbacks_.encodeData(data, end_stream);
            maybeEndEncode(end_stream);
          }},
      Utility::LocalReplyData{Grpc::Common::hasGrpcContentType(*request_headers_), code, body,
                              grpc_status, is_head_request});
  maybeEndEncode(state_.local_complete_);
}

void ConnectionManagerImpl::FilterManager::encode100ContinueHeaders(
    ActiveStreamEncoderFilter* filter, ResponseHeaderMap& headers) {
  active_stream_.resetIdleTimer();
  ASSERT(active_stream_.connection_manager_.config_.proxy100Continue());
  // The caller must guarantee that encode100ContinueHeaders() is invoked at most once.
  ASSERT(!state_.has_continue_headers_ || filter != nullptr);
  // Make sure commonContinue continues encode100ContinueHeaders.
  state_.has_continue_headers_ = true;

  // Similar to the block in encodeHeaders, run encode100ContinueHeaders on each
  // filter. This is simpler than that case because 100 continue implies no
  // end-stream, and because there are normal headers coming there's no need for
  // complex continuation logic.
  // 100-continue filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, false, FilterIterationStartState::AlwaysStartFromNext);
  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::Encode100ContinueHeaders));
    state_.filter_call_state_ |= FilterCallState::Encode100ContinueHeaders;
    FilterHeadersStatus status = (*entry)->handle_->encode100ContinueHeaders(headers);
    state_.filter_call_state_ &= ~FilterCallState::Encode100ContinueHeaders;
    ENVOY_STREAM_LOG(trace, "encode 100 continue headers called: filter={} status={}",
                     active_stream_, static_cast<const void*>((*entry).get()),
                     static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfter100ContinueHeadersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encode100ContinueHeaders(headers);
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

void ConnectionManagerImpl::FilterManager::maybeContinueEncoding(
    const std::list<ActiveStreamEncoderFilterPtr>::iterator& continue_data_entry) {
  if (continue_data_entry != encoder_filters_.end()) {
    // We use the continueEncoding() code since it will correctly handle not calling
    // encodeHeaders() again. Fake setting StopSingleIteration since the continueEncoding() code
    // expects it.
    ASSERT(buffered_response_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueEncoding();
  }
}

void ConnectionManagerImpl::FilterManager::encodeHeaders(ActiveStreamEncoderFilter* filter,
                                                         ResponseHeaderMap& headers,
                                                         bool end_stream) {
  // See encodeHeaders() comments in include/envoy/http/filter.h for why the 1xx precondition holds.
  ASSERT(!CodeUtility::is1xx(Utility::getResponseStatus(headers)) ||
         Utility::getResponseStatus(headers) == enumToInt(Http::Code::SwitchingProtocols));
  active_stream_.resetIdleTimer();
  disarmRequestTimeout();

  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamEncoderFilterPtr>::iterator continue_data_entry = encoder_filters_.end();

  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeHeaders));
    state_.filter_call_state_ |= FilterCallState::EncodeHeaders;
    (*entry)->end_stream_ = state_.encoding_headers_only_ ||
                            (end_stream && continue_data_entry == encoder_filters_.end());
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(headers, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeHeaders;
    ENVOY_STREAM_LOG(trace, "encode headers called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    (*entry)->encode_headers_called_ = true;
    const auto continue_iteration =
        (*entry)->commonHandleAfterHeadersCallback(status, state_.encoding_headers_only_);

    // If we're encoding a headers only response, then mark the local as complete. This ensures
    // that we don't attempt to reset the downstream request in doEndStream.
    if (state_.encoding_headers_only_) {
      state_.local_complete_ = true;
    }

    if (!continue_iteration) {
      if (!(*entry)->end_stream_) {
        maybeContinueEncoding(continue_data_entry);
      }
      return;
    }

    // Here we handle the case where we have a header only response, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_response_data_ && continue_data_entry == encoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  const bool modified_end_stream = state_.encoding_headers_only_ ||
                                   (end_stream && continue_data_entry == encoder_filters_.end());
  filter_manager_callbacks_.encodeHeaders(headers, modified_end_stream);
  maybeEndEncode(modified_end_stream);

  if (!modified_end_stream) {
    maybeContinueEncoding(continue_data_entry);
  }
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

  // See if we want to drain/close the connection. Send the go away frame prior to encoding the
  // header block.
  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.drain_close_.drainClose()) {

    // This doesn't really do anything for HTTP/1.1 other then give the connection another boost
    // of time to race with incoming requests. It mainly just keeps the logic the same between
    // HTTP/1.1 and HTTP/2.
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

  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.overload_disable_keepalive_ref_.isSaturated()) {
    ENVOY_STREAM_LOG(debug, "disabling keepalive due to envoy overload", *this);
    connection_manager_.drain_state_ = DrainState::Closing;
    connection_manager_.stats_.named_.downstream_cx_overload_disable_keepalive_.inc();
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

  // 100-continue headers are handled via encode100ContinueHeaders.
  state_.non_100_response_headers_encoded_ = true;
  chargeStats(headers);

  ENVOY_STREAM_LOG(debug, "encoding headers via codec (end_stream={}):\n{}", *this, end_stream,
                   headers);

  // Now actually encode via the codec.
  filter_manager_.streamInfo().onFirstDownstreamTxByteSent();
  response_encoder_->encodeHeaders(headers, end_stream);
}

void ConnectionManagerImpl::FilterManager::encodeMetadata(ActiveStreamEncoderFilter* filter,
                                                          MetadataMapPtr&& metadata_map_ptr) {
  active_stream_.resetIdleTimer();

  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, false, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, stores metadata and returns.
    // If the filter pointed by entry hasn't returned from encodeHeaders, stores newly added
    // metadata in case encodeHeaders returns StopAllIteration. The latter can happen when headers
    // callbacks generate new metadata.
    if (!(*entry)->encode_headers_called_ || (*entry)->stoppedAll()) {
      (*entry)->getSavedResponseMetadata()->emplace_back(std::move(metadata_map_ptr));
      return;
    }

    FilterMetadataStatus status = (*entry)->handle_->encodeMetadata(*metadata_map_ptr);
    ENVOY_STREAM_LOG(trace, "encode metadata called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
  }
  // TODO(soya3129): update stats with metadata.

  // Now encode metadata via the codec.
  if (!metadata_map_ptr->empty()) {
    MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    filter_manager_callbacks_.encodeMetadata(metadata_map_vector);
  }
}

ResponseTrailerMap& ConnectionManagerImpl::FilterManager::addEncodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!response_trailers_);

  response_trailers_ = ResponseTrailerMapImpl::create();
  return *response_trailers_;
}

void ConnectionManagerImpl::FilterManager::sendLocalReply(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  active_stream_.sendLocalReply(is_grpc_request, code, body, modify_headers, grpc_status, details);
}

void ConnectionManagerImpl::FilterManager::addEncodedData(ActiveStreamEncoderFilter& filter,
                                                          Buffer::Instance& data, bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::EncodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::EncodeData) ||
      ((state_.filter_call_state_ & FilterCallState::EncodeTrailers) && !filter.canIterate())) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.encoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::EncodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    encodeData(&filter, data, false, FilterIterationStartState::AlwaysStartFromNext);
  } else {
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

void ConnectionManagerImpl::FilterManager::encodeData(
    ActiveStreamEncoderFilter* filter, Buffer::Instance& data, bool end_stream,
    FilterIterationStartState filter_iteration_start_state) {
  active_stream_.resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (state_.encoding_headers_only_) {
    return;
  }

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, filter_iteration_start_state);
  auto trailers_added_entry = encoder_filters_.end();

  const bool trailers_exists_at_start = response_trailers_ != nullptr;
  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if (handleDataIfStopAll(**entry, data, state_.encoder_filters_streaming_)) {
      return;
    }
    // If end_stream_ is marked for a filter, the data is not for this filter and filters after.
    // For details, please see the comment in the ActiveStream::decodeData() function.
    if ((*entry)->end_stream_) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeData));

    // We check the response_trailers_ pointer here in case addEncodedTrailers
    // is called in encodeData during a previous filter invocation, at which point we communicate to
    // the current and future filters that the stream has not yet ended.
    state_.filter_call_state_ |= FilterCallState::EncodeData;
    if (end_stream) {
      state_.filter_call_state_ |= FilterCallState::LastDataFrame;
    }

    recordLatestDataFilter(entry, state_.latest_data_encoding_filter_, encoder_filters_);

    (*entry)->end_stream_ = end_stream && !response_trailers_;
    FilterDataStatus status = (*entry)->handle_->encodeData(data, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "encode data called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    if (!trailers_exists_at_start && response_trailers_ &&
        trailers_added_entry == encoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.encoder_filters_streaming_)) {
      return;
    }
  }

  const bool modified_end_stream = end_stream && trailers_added_entry == encoder_filters_.end();
  ASSERT(!state_.encoding_headers_only_);
  filter_manager_callbacks_.encodeData(data, modified_end_stream);
  maybeEndEncode(modified_end_stream);

  // If trailers were adding during encodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != encoder_filters_.end()) {
    encodeTrailers(trailers_added_entry->get(), *response_trailers_);
  }
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

void ConnectionManagerImpl::FilterManager::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                                          ResponseTrailerMap& trailers) {
  active_stream_.resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (state_.encoding_headers_only_) {
    return;
  }

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, true, FilterIterationStartState::CanStartFromCurrent);
  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if ((*entry)->stoppedAll()) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeTrailers));
    state_.filter_call_state_ |= FilterCallState::EncodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->encodeTrailers(trailers);
    (*entry)->handle_->encodeComplete();
    (*entry)->end_stream_ = true;
    state_.filter_call_state_ &= ~FilterCallState::EncodeTrailers;
    ENVOY_STREAM_LOG(trace, "encode trailers called: filter={} status={}", active_stream_,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encodeTrailers(trailers);
  maybeEndEncode(true);
}

void ConnectionManagerImpl::FilterManager::maybeEndEncode(bool end_stream) {
  if (end_stream) {
    filter_manager_callbacks_.endStream();
  }
}

bool ConnectionManagerImpl::FilterManager::processNewlyAddedMetadata() {
  if (request_metadata_map_vector_ == nullptr) {
    return false;
  }
  for (const auto& metadata_map : *getRequestMetadataMapVector()) {
    decodeMetadata(nullptr, *metadata_map);
  }
  getRequestMetadataMapVector()->clear();
  return true;
}

bool ConnectionManagerImpl::FilterManager::handleDataIfStopAll(ActiveStreamFilterBase& filter,
                                                               Buffer::Instance& data,
                                                               bool& filter_streaming) {
  if (filter.stoppedAll()) {
    ASSERT(!filter.canIterate());
    filter_streaming =
        filter.iteration_state_ == ActiveStreamFilterBase::IterationState::StopAllWatermark;
    filter.commonHandleBufferData(data);
    return true;
  }
  return false;
}

void ConnectionManagerImpl::ActiveStream::onResetStream(StreamResetReason, absl::string_view) {
  // NOTE: This function gets called in all of the following cases:
  //       1) We TX an app level reset
  //       2) The codec TX a codec level reset
  //       3) The codec RX a reset
  //       If we need to differentiate we need to do it inside the codec. Can start with this.
  ENVOY_STREAM_LOG(debug, "stream reset", *this);
  connection_manager_.stats_.named_.downstream_rq_rx_reset_.inc();
  connection_manager_.doDeferredStreamDestroy(*this);

  // If the codec sets its responseDetails(), impute a
  // DownstreamProtocolError and propagate the details upwards.
  const absl::string_view encoder_details = response_encoder_->getStream().responseDetails();
  if (!encoder_details.empty()) {
    filter_manager_.streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError);
    filter_manager_.streamInfo().setResponseCodeDetails(encoder_details);
  }
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

void ConnectionManagerImpl::FilterManager::callHighWatermarkCallbacks() {
  ++high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionManagerImpl::FilterManager::callLowWatermarkCallbacks() {
  ASSERT(high_watermark_count_ > 0);
  --high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionManagerImpl::FilterManager::setBufferLimit(uint32_t new_limit) {
  ENVOY_STREAM_LOG(debug, "setting buffer limit to {}", active_stream_, new_limit);
  buffer_limit_ = new_limit;
  if (buffered_request_data_) {
    buffered_request_data_->setWatermarks(buffer_limit_);
  }
  if (buffered_response_data_) {
    buffered_response_data_->setWatermarks(buffer_limit_);
  }
}

bool ConnectionManagerImpl::FilterManager::createFilterChain() {
  if (state_.created_filter_chain_) {
    return false;
  }
  bool upgrade_rejected = false;
  const HeaderEntry* upgrade = nullptr;
  if (request_headers_) {
    upgrade = request_headers_->Upgrade();

    // Treat CONNECT requests as a special upgrade case.
    if (!upgrade && HeaderUtility::isConnect(*request_headers_)) {
      upgrade = request_headers_->Method();
    }
  }

  state_.created_filter_chain_ = true;
  if (upgrade != nullptr) {
    const Router::RouteEntry::UpgradeMap* upgrade_map = nullptr;

    // We must check if the 'cached_route_' optional is populated since this function can be called
    // early via sendLocalReply(), before the cached route is populated.
    if (active_stream_.hasCachedRoute() && active_stream_.cached_route_.value()->routeEntry()) {
      upgrade_map = &active_stream_.cached_route_.value()->routeEntry()->upgradeMap();
    }

    if (filter_chain_factory_.createUpgradeFilterChain(upgrade->value().getStringView(),
                                                       upgrade_map, *this)) {
      filter_manager_callbacks_.upgradeFilterChainCreated();
      return true;
    } else {
      upgrade_rejected = true;
      // Fall through to the default filter chain. The function calling this
      // will send a local reply indicating that the upgrade failed.
    }
  }

  filter_chain_factory_.createFilterChain(*this);
  return !upgrade_rejected;
}

void ConnectionManagerImpl::ActiveStreamFilterBase::commonContinue() {
  // TODO(mattklein123): Raise an error if this is called during a callback.
  if (!canContinue()) {
    ENVOY_STREAM_LOG(trace, "cannot continue filter chain: filter={}", parent_.active_stream_,
                     static_cast<const void*>(this));
    return;
  }

  ENVOY_STREAM_LOG(trace, "continuing filter chain: filter={}", parent_.active_stream_,
                   static_cast<const void*>(this));
  ASSERT(!canIterate());
  // If iteration has stopped for all frame types, set iterate_from_current_filter_ to true so the
  // filter iteration starts with the current filter instead of the next one.
  if (stoppedAll()) {
    iterate_from_current_filter_ = true;
  }
  allowIteration();

  // Only resume with do100ContinueHeaders() if we've actually seen a 100-Continue.
  if (has100Continueheaders()) {
    continue_headers_continued_ = true;
    do100ContinueHeaders();
    // If the response headers have not yet come in, don't continue on with
    // headers and body. doHeaders expects request headers to exist.
    if (!parent_.response_headers_.get()) {
      return;
    }
  }

  // Make sure that we handle the zero byte data frame case. We make no effort to optimize this
  // case in terms of merging it into a header only request/response. This could be done in the
  // future.
  if (!headers_continued_) {
    headers_continued_ = true;
    doHeaders(complete() && !bufferedData() && !hasTrailers());
  }

  doMetadata();

  if (bufferedData()) {
    doData(complete() && !hasTrailers());
  }

  if (hasTrailers()) {
    doTrailers();
  }

  iterate_from_current_filter_ = false;
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfter100ContinueHeadersCallback(
    FilterHeadersStatus status) {
  ASSERT(parent_.state_.has_continue_headers_);
  ASSERT(!continue_headers_continued_);
  ASSERT(canIterate());

  if (status == FilterHeadersStatus::StopIteration) {
    iteration_state_ = IterationState::StopSingleIteration;
    return false;
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    continue_headers_continued_ = true;
    return true;
  }
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterHeadersCallback(
    FilterHeadersStatus status, bool& headers_only) {
  ASSERT(!headers_continued_);
  ASSERT(canIterate());

  if (status == FilterHeadersStatus::StopIteration) {
    iteration_state_ = IterationState::StopSingleIteration;
  } else if (status == FilterHeadersStatus::StopAllIterationAndBuffer) {
    iteration_state_ = IterationState::StopAllBuffer;
  } else if (status == FilterHeadersStatus::StopAllIterationAndWatermark) {
    iteration_state_ = IterationState::StopAllWatermark;
  } else if (status == FilterHeadersStatus::ContinueAndEndStream) {
    // Set headers_only to true so we know to end early if necessary,
    // but continue filter iteration so we actually write the headers/run the cleanup code.
    headers_only = true;
    ENVOY_STREAM_LOG(debug, "converting to headers only", parent_.active_stream_);
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    headers_continued_ = true;
  }

  handleMetadataAfterHeadersCallback();

  if (stoppedAll() || status == FilterHeadersStatus::StopIteration) {
    return false;
  } else {
    return true;
  }
}

void ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleBufferData(
    Buffer::Instance& provided_data) {

  // The way we do buffering is a little complicated which is why we have this common function
  // which is used for both encoding and decoding. When data first comes into our filter pipeline,
  // we send it through. Any filter can choose to stop iteration and buffer or not. If we then
  // continue iteration in the future, we use the buffered data. A future filter can stop and
  // buffer again. In this case, since we are already operating on buffered data, we don't
  // rebuffer, because we assume the filter has modified the buffer as it wishes in place.
  if (bufferedData().get() != &provided_data) {
    if (!bufferedData()) {
      bufferedData() = createBuffer();
    }
    bufferedData()->move(provided_data);
  }
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterDataCallback(
    FilterDataStatus status, Buffer::Instance& provided_data, bool& buffer_was_streaming) {

  if (status == FilterDataStatus::Continue) {
    if (iteration_state_ == IterationState::StopSingleIteration) {
      commonHandleBufferData(provided_data);
      commonContinue();
      return false;
    } else {
      ASSERT(headers_continued_);
    }
  } else {
    iteration_state_ = IterationState::StopSingleIteration;
    if (status == FilterDataStatus::StopIterationAndBuffer ||
        status == FilterDataStatus::StopIterationAndWatermark) {
      buffer_was_streaming = status == FilterDataStatus::StopIterationAndWatermark;
      commonHandleBufferData(provided_data);
    } else if (complete() && !hasTrailers() && !bufferedData()) {
      // If this filter is doing StopIterationNoBuffer and this stream is terminated with a zero
      // byte data frame, we need to create an empty buffer to make sure that when commonContinue
      // is called, the pipeline resumes with an empty data frame with end_stream = true
      ASSERT(end_stream_);
      bufferedData() = createBuffer();
    }

    return false;
  }

  return true;
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterTrailersCallback(
    FilterTrailersStatus status) {

  if (status == FilterTrailersStatus::Continue) {
    if (iteration_state_ == IterationState::StopSingleIteration) {
      commonContinue();
      return false;
    } else {
      ASSERT(headers_continued_);
    }
  } else {
    return false;
  }

  return true;
}

const Network::Connection* ConnectionManagerImpl::ActiveStreamFilterBase::connection() {
  return parent_.active_stream_.connection();
}

Event::Dispatcher& ConnectionManagerImpl::ActiveStreamFilterBase::dispatcher() {
  return parent_.active_stream_.connection_manager_.read_callbacks_->connection().dispatcher();
}

StreamInfo::StreamInfo& ConnectionManagerImpl::ActiveStreamFilterBase::streamInfo() {
  return parent_.stream_info_;
}

Tracing::Span& ConnectionManagerImpl::ActiveStreamFilterBase::activeSpan() {
  if (parent_.active_stream_.active_span_) {
    return *parent_.active_stream_.active_span_;
  } else {
    return Tracing::NullSpan::instance();
  }
}

Tracing::Config& ConnectionManagerImpl::ActiveStreamFilterBase::tracingConfig() {
  return parent_.active_stream_;
}

const ScopeTrackedObject& ConnectionManagerImpl::ActiveStreamFilterBase::scope() {
  return parent_.active_stream_;
}

Upstream::ClusterInfoConstSharedPtr ConnectionManagerImpl::ActiveStreamFilterBase::clusterInfo() {
  // NOTE: Refreshing route caches clusterInfo as well.
  if (!parent_.active_stream_.cached_route_.has_value()) {
    parent_.active_stream_.refreshCachedRoute();
  }

  return parent_.active_stream_.cached_cluster_info_.value();
}

Router::RouteConstSharedPtr ConnectionManagerImpl::ActiveStreamFilterBase::route() {
  return route(nullptr);
}

Router::RouteConstSharedPtr
ConnectionManagerImpl::ActiveStreamFilterBase::route(const Router::RouteCallback& cb) {
  if (parent_.active_stream_.cached_route_.has_value()) {
    return parent_.active_stream_.cached_route_.value();
  }
  parent_.active_stream_.refreshCachedRoute(cb);
  return parent_.active_stream_.cached_route_.value();
}

void ConnectionManagerImpl::ActiveStreamFilterBase::clearRouteCache() {
  parent_.active_stream_.cached_route_ = absl::optional<Router::RouteConstSharedPtr>();
  parent_.active_stream_.cached_cluster_info_ =
      absl::optional<Upstream::ClusterInfoConstSharedPtr>();
  if (parent_.active_stream_.tracing_custom_tags_) {
    parent_.active_stream_.tracing_custom_tags_->clear();
  }
}

bool ConnectionManagerImpl::ActiveStreamDecoderFilter::canContinue() {
  // It is possible for the connection manager to respond directly to a request even while
  // a filter is trying to continue. If a response has already happened, we should not
  // continue to further filters. A concrete example of this is a filter buffering data, the
  // last data frame comes in and the filter continues, but the final buffering takes the stream
  // over the high watermark such that a 413 is returned.
  return !parent_.state_.local_complete_;
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamDecoderFilter::createBuffer() {
  auto buffer = std::make_unique<Buffer::WatermarkBuffer>(
      [this]() -> void { this->requestDataDrained(); },
      [this]() -> void { this->requestDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
}

Buffer::WatermarkBufferPtr& ConnectionManagerImpl::ActiveStreamDecoderFilter::bufferedData() {
  return parent_.buffered_request_data_;
}

bool ConnectionManagerImpl::ActiveStreamDecoderFilter::complete() {
  return parent_.state_.remote_complete_;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::doHeaders(bool end_stream) {
  parent_.decodeHeaders(this, *parent_.request_headers_, end_stream);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::doData(bool end_stream) {
  parent_.decodeData(this, *parent_.buffered_request_data_, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::doTrailers() {
  parent_.decodeTrailers(this, *parent_.request_trailers_);
}
bool ConnectionManagerImpl::ActiveStreamDecoderFilter::hasTrailers() {
  return parent_.request_trailers_ != nullptr;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::drainSavedRequestMetadata() {
  ASSERT(saved_request_metadata_ != nullptr);
  for (auto& metadata_map : *getSavedRequestMetadata()) {
    parent_.decodeMetadata(this, *metadata_map);
  }
  getSavedRequestMetadata()->clear();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::handleMetadataAfterHeadersCallback() {
  // If we drain accumulated metadata, the iteration must start with the current filter.
  const bool saved_state = iterate_from_current_filter_;
  iterate_from_current_filter_ = true;
  // If decodeHeaders() returns StopAllIteration, we should skip draining metadata, and wait
  // for doMetadata() to drain the metadata after iteration continues.
  if (!stoppedAll() && saved_request_metadata_ != nullptr && !getSavedRequestMetadata()->empty()) {
    drainSavedRequestMetadata();
  }
  // Restores the original value of iterate_from_current_filter_.
  iterate_from_current_filter_ = saved_state;
}

RequestTrailerMap& ConnectionManagerImpl::ActiveStreamDecoderFilter::addDecodedTrailers() {
  return parent_.addDecodedTrailers();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::addDecodedData(Buffer::Instance& data,
                                                                      bool streaming) {
  parent_.addDecodedData(*this, data, streaming);
}

MetadataMapVector& ConnectionManagerImpl::ActiveStreamDecoderFilter::addDecodedMetadata() {
  return parent_.addDecodedMetadata();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::injectDecodedDataToFilterChain(
    Buffer::Instance& data, bool end_stream) {
  parent_.decodeData(this, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::continueDecoding() { commonContinue(); }
const Buffer::Instance* ConnectionManagerImpl::ActiveStreamDecoderFilter::decodingBuffer() {
  return parent_.buffered_request_data_.get();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::modifyDecodingBuffer(
    std::function<void(Buffer::Instance&)> callback) {
  ASSERT(parent_.state_.latest_data_decoding_filter_ == this);
  callback(*parent_.buffered_request_data_.get());
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::sendLocalReply(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  parent_.stream_info_.setResponseCodeDetails(details);
  parent_.sendLocalReply(is_grpc_request_, code, body, modify_headers, grpc_status, details);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encode100ContinueHeaders(
    ResponseHeaderMapPtr&& headers) {
  // If Envoy is not configured to proxy 100-Continue responses, swallow the 100 Continue
  // here. This avoids the potential situation where Envoy strips Expect: 100-Continue and sends a
  // 100-Continue, then proxies a duplicate 100 Continue from upstream.
  if (parent_.active_stream_.connection_manager_.config_.proxy100Continue()) {
    parent_.continue_headers_ = std::move(headers);
    parent_.encode100ContinueHeaders(nullptr, *parent_.continue_headers_);
  }
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeHeaders(ResponseHeaderMapPtr&& headers,
                                                                     bool end_stream) {
  parent_.response_headers_ = std::move(headers);
  parent_.encodeHeaders(nullptr, *parent_.response_headers_, end_stream);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeData(Buffer::Instance& data,
                                                                  bool end_stream) {
  parent_.encodeData(nullptr, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeTrailers(
    ResponseTrailerMapPtr&& trailers) {
  parent_.response_trailers_ = std::move(trailers);
  parent_.encodeTrailers(nullptr, *parent_.response_trailers_);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeMetadata(
    MetadataMapPtr&& metadata_map_ptr) {
  parent_.encodeMetadata(nullptr, std::move(metadata_map_ptr));
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::
    onDecoderFilterAboveWriteBufferHighWatermark() {
  parent_.filter_manager_callbacks_.onDecoderFilterAboveWriteBufferHighWatermark();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::requestDataTooLarge() {
  ENVOY_STREAM_LOG(debug, "request data too large watermark exceeded", parent_.active_stream_);
  if (parent_.state_.decoder_filters_streaming_) {
    onDecoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.active_stream_.connection_manager_.stats_.named_.downstream_rq_too_large_.inc();
    sendLocalReply(Code::PayloadTooLarge, CodeUtility::toString(Code::PayloadTooLarge), nullptr,
                   absl::nullopt, StreamInfo::ResponseCodeDetails::get().RequestPayloadTooLarge);
  }
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::requestDataDrained() {
  // If this is called it means the call to requestDataTooLarge() was a
  // streaming call, or a 413 would have been sent.
  onDecoderFilterBelowWriteBufferLowWatermark();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::
    onDecoderFilterBelowWriteBufferLowWatermark() {
  parent_.filter_manager_callbacks_.onDecoderFilterBelowWriteBufferLowWatermark();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::addDownstreamWatermarkCallbacks(
    DownstreamWatermarkCallbacks& watermark_callbacks) {
  // This is called exactly once per upstream-stream, by the router filter. Therefore, we
  // expect the same callbacks to not be registered twice.
  ASSERT(std::find(parent_.watermark_callbacks_.begin(), parent_.watermark_callbacks_.end(),
                   &watermark_callbacks) == parent_.watermark_callbacks_.end());
  parent_.watermark_callbacks_.emplace(parent_.watermark_callbacks_.end(), &watermark_callbacks);
  for (uint32_t i = 0; i < parent_.high_watermark_count_; ++i) {
    watermark_callbacks.onAboveWriteBufferHighWatermark();
  }
}
void ConnectionManagerImpl::ActiveStreamDecoderFilter::removeDownstreamWatermarkCallbacks(
    DownstreamWatermarkCallbacks& watermark_callbacks) {
  ASSERT(std::find(parent_.watermark_callbacks_.begin(), parent_.watermark_callbacks_.end(),
                   &watermark_callbacks) != parent_.watermark_callbacks_.end());
  parent_.watermark_callbacks_.remove(&watermark_callbacks);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::setDecoderBufferLimit(uint32_t limit) {
  parent_.setBufferLimit(limit);
}

uint32_t ConnectionManagerImpl::ActiveStreamDecoderFilter::decoderBufferLimit() {
  return parent_.buffer_limit_;
}

bool ConnectionManagerImpl::ActiveStreamDecoderFilter::recreateStream() {
  // Because the filter's and the HCM view of if the stream has a body and if
  // the stream is complete may differ, re-check bytesReceived() to make sure
  // there was no body from the HCM's point of view.
  if (!complete() || parent_.stream_info_.bytesReceived() != 0) {
    return false;
  }

  parent_.stream_info_.setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().InternalRedirect);
  // n.b. we do not currently change the codecs to point at the new stream
  // decoder because the decoder callbacks are complete. It would be good to
  // null out that pointer but should not be necessary.
  RequestHeaderMapPtr request_headers(std::move(parent_.request_headers_));
  ResponseEncoder* response_encoder = parent_.active_stream_.response_encoder_;
  parent_.active_stream_.response_encoder_ = nullptr;
  response_encoder->getStream().removeCallbacks(parent_.active_stream_);
  // This functionally deletes the stream (via deferred delete) so do not
  // reference anything beyond this point.
  parent_.active_stream_.connection_manager_.doEndStream(parent_.active_stream_);

  RequestDecoder& new_stream =
      parent_.active_stream_.connection_manager_.newStream(*response_encoder, true);
  // We don't need to copy over the old parent FilterState from the old StreamInfo if it did not
  // store any objects with a LifeSpan at or above DownstreamRequest. This is to avoid unnecessary
  // heap allocation.
  // TODO(snowp): In the case where connection level filter state has been set on the connection
  // FilterState that we inherit, we'll end up copying this every time even though we could get
  // away with just resetting it to the HCM filter_state_.
  if (parent_.stream_info_.filter_state_->hasDataAtOrAboveLifeSpan(
          StreamInfo::FilterState::LifeSpan::Request)) {
    (*parent_.active_stream_.connection_manager_.streams_.begin())
        ->filter_manager_.streamInfo()
        .filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        parent_.stream_info_.filter_state_->parent(),
        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  new_stream.decodeHeaders(std::move(request_headers), true);
  return true;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::addUpstreamSocketOptions(
    const Network::Socket::OptionsSharedPtr& options) {

  Network::Socket::appendOptions(parent_.active_stream_.upstream_options_, options);
}

Network::Socket::OptionsSharedPtr
ConnectionManagerImpl::ActiveStreamDecoderFilter::getUpstreamSocketOptions() const {
  return parent_.active_stream_.upstream_options_;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::requestRouteConfigUpdate(
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  parent_.active_stream_.requestRouteConfigUpdate(dispatcher(), std::move(route_config_updated_cb));
}

absl::optional<Router::ConfigConstSharedPtr>
ConnectionManagerImpl::ActiveStreamDecoderFilter::routeConfig() {
  return parent_.active_stream_.routeConfig();
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamEncoderFilter::createBuffer() {
  auto buffer = new Buffer::WatermarkBuffer(
      [this]() -> void { this->responseDataDrained(); },
      [this]() -> void { this->responseDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return Buffer::WatermarkBufferPtr{buffer};
}
Buffer::WatermarkBufferPtr& ConnectionManagerImpl::ActiveStreamEncoderFilter::bufferedData() {
  return parent_.buffered_response_data_;
}
bool ConnectionManagerImpl::ActiveStreamEncoderFilter::complete() {
  return parent_.state_.local_complete_;
}
bool ConnectionManagerImpl::ActiveStreamEncoderFilter::has100Continueheaders() {
  return parent_.state_.has_continue_headers_ && !continue_headers_continued_;
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::do100ContinueHeaders() {
  parent_.encode100ContinueHeaders(this, *parent_.continue_headers_);
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::doHeaders(bool end_stream) {
  parent_.encodeHeaders(this, *parent_.response_headers_, end_stream);
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::doData(bool end_stream) {
  parent_.encodeData(this, *parent_.buffered_response_data_, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::drainSavedResponseMetadata() {
  ASSERT(saved_response_metadata_ != nullptr);
  for (auto& metadata_map : *getSavedResponseMetadata()) {
    parent_.encodeMetadata(this, std::move(metadata_map));
  }
  getSavedResponseMetadata()->clear();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::handleMetadataAfterHeadersCallback() {
  // If we drain accumulated metadata, the iteration must start with the current filter.
  const bool saved_state = iterate_from_current_filter_;
  iterate_from_current_filter_ = true;
  // If encodeHeaders() returns StopAllIteration, we should skip draining metadata, and wait
  // for doMetadata() to drain the metadata after iteration continues.
  if (!stoppedAll() && saved_response_metadata_ != nullptr &&
      !getSavedResponseMetadata()->empty()) {
    drainSavedResponseMetadata();
  }
  // Restores the original value of iterate_from_current_filter_.
  iterate_from_current_filter_ = saved_state;
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::doTrailers() {
  parent_.encodeTrailers(this, *parent_.response_trailers_);
}
bool ConnectionManagerImpl::ActiveStreamEncoderFilter::hasTrailers() {
  return parent_.response_trailers_ != nullptr;
}
void ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedData(Buffer::Instance& data,
                                                                      bool streaming) {
  return parent_.addEncodedData(*this, data, streaming);
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::injectEncodedDataToFilterChain(
    Buffer::Instance& data, bool end_stream) {
  parent_.encodeData(this, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

ResponseTrailerMap& ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedTrailers() {
  return parent_.addEncodedTrailers();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedMetadata(
    MetadataMapPtr&& metadata_map_ptr) {
  return parent_.encodeMetadata(this, std::move(metadata_map_ptr));
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::
    onEncoderFilterAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Disabling upstream stream due to filter callbacks.",
                   parent_.active_stream_);
  parent_.callHighWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::
    onEncoderFilterBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Enabling upstream stream due to filter callbacks.",
                   parent_.active_stream_);
  parent_.callLowWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::setEncoderBufferLimit(uint32_t limit) {
  parent_.setBufferLimit(limit);
}

uint32_t ConnectionManagerImpl::ActiveStreamEncoderFilter::encoderBufferLimit() {
  return parent_.buffer_limit_;
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::continueEncoding() { commonContinue(); }

const Buffer::Instance* ConnectionManagerImpl::ActiveStreamEncoderFilter::encodingBuffer() {
  return parent_.buffered_response_data_.get();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::modifyEncodingBuffer(
    std::function<void(Buffer::Instance&)> callback) {
  ASSERT(parent_.state_.latest_data_encoding_filter_ == this);
  callback(*parent_.buffered_response_data_.get());
}

Http1StreamEncoderOptionsOptRef
ConnectionManagerImpl::ActiveStreamEncoderFilter::http1StreamEncoderOptions() {
  // TODO(mattklein123): At some point we might want to actually wrap this interface but for now
  // we give the filter direct access to the encoder options.
  return parent_.active_stream_.response_encoder_->http1StreamEncoderOptions();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::responseDataTooLarge() {
  if (parent_.state_.encoder_filters_streaming_) {
    onEncoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.active_stream_.connection_manager_.stats_.named_.rs_too_large_.inc();

    // In this case, sendLocalReply will either send a response directly to the encoder, or
    // reset the stream.
    parent_.sendLocalReply(
        parent_.request_headers_ && Grpc::Common::isGrpcRequestHeaders(*parent_.request_headers_),
        Http::Code::InternalServerError, CodeUtility::toString(Http::Code::InternalServerError),
        nullptr, absl::nullopt, StreamInfo::ResponseCodeDetails::get().ResponsePayloadTooLarge);
  }
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::responseDataDrained() {
  onEncoderFilterBelowWriteBufferLowWatermark();
}

void ConnectionManagerImpl::ActiveStreamFilterBase::resetStream() {
  parent_.active_stream_.connection_manager_.stats_.named_.downstream_rq_tx_reset_.inc();
  parent_.active_stream_.connection_manager_.doEndStream(parent_.active_stream_);
}

uint64_t ConnectionManagerImpl::ActiveStreamFilterBase::streamId() const {
  return parent_.active_stream_.stream_id_;
}

} // namespace Http
} // namespace Envoy
