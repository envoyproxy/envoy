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
#include "envoy/network/drain_decision.h"
#include "envoy/router/router.h"
#include "envoy/server/admin.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/http_tracer.h"

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
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/path_utility.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/router/config_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/timespan_impl.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"

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
  return {
      {ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix),
                               POOL_HISTOGRAM_PREFIX(scope, prefix))},
      prefix,
      scope};
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
                                             Runtime::RandomGenerator& random_generator,
                                             Http::Context& http_context, Runtime::Loader& runtime,
                                             const LocalInfo::LocalInfo& local_info,
                                             Upstream::ClusterManager& cluster_manager,
                                             Server::OverloadManager* overload_manager,
                                             TimeSource& time_source)
    : config_(config), stats_(config_.stats()),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          stats_.named_.downstream_cx_length_ms_, time_source)),
      drain_close_(drain_close), random_generator_(random_generator), http_context_(http_context),
      runtime_(runtime), local_info_(local_info), cluster_manager_(cluster_manager),
      listener_stats_(config_.listenerStats()),
      overload_stop_accepting_requests_ref_(
          overload_manager ? overload_manager->getThreadLocalOverloadState().getState(
                                 Server::OverloadActionNames::get().StopAcceptingRequests)
                           : Server::OverloadManager::getInactiveState()),
      overload_disable_keepalive_ref_(
          overload_manager ? overload_manager->getThreadLocalOverloadState().getState(
                                 Server::OverloadActionNames::get().DisableHttpKeepAlive)
                           : Server::OverloadManager::getInactiveState()),
      time_source_(time_source) {}

const HeaderMapImpl& ConnectionManagerImpl::continueHeader() {
  CONSTRUCT_ON_FIRST_USE(HeaderMapImpl,
                         {Http::Headers::get().Status, std::to_string(enumToInt(Code::Continue))});
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
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWriteAndDelay);
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
      (!stream.state_.remote_complete_ || !stream.state_.codec_saw_local_complete_)) {
    // Indicate local is complete at this point so that if we reset during a continuation, we don't
    // raise further data or trailers.
    ENVOY_STREAM_LOG(debug, "doEndStream() resetting stream", stream);
    stream.state_.local_complete_ = true;
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

  // Reading may have been disabled for the non-multiplexing case, so enable it again.
  // Also be sure to unwind any read-disable done by the prior downstream
  // connection.
  if (drain_state_ != DrainState::Closing && codec_->protocol() < Protocol::Http2) {
    while (!read_callbacks_->connection().readEnabled()) {
      read_callbacks_->connection().readDisable(false);
    }
  }
}

void ConnectionManagerImpl::doDeferredStreamDestroy(ActiveStream& stream) {
  if (stream.stream_idle_timer_ != nullptr) {
    stream.stream_idle_timer_->disableTimer();
    stream.stream_idle_timer_ = nullptr;
  }
  stream.disarmRequestTimeout();

  stream.state_.destroyed_ = true;
  for (auto& filter : stream.decoder_filters_) {
    filter->handle_->onDestroy();
  }

  for (auto& filter : stream.encoder_filters_) {
    // Do not call on destroy twice for dual registered filters.
    if (!filter->dual_filter_) {
      filter->handle_->onDestroy();
    }
  }

  read_callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(streams_));

  if (connection_idle_timer_ && streams_.empty()) {
    connection_idle_timer_->enableTimer(config_.idleTimeout().value());
  }
}

StreamDecoder& ConnectionManagerImpl::newStream(StreamEncoder& response_encoder,
                                                bool is_internally_created) {
  if (connection_idle_timer_) {
    connection_idle_timer_->disableTimer();
  }

  ENVOY_CONN_LOG(debug, "new stream", read_callbacks_->connection());
  ActiveStreamPtr new_stream(new ActiveStream(*this));
  new_stream->state_.is_internally_created_ = is_internally_created;
  new_stream->response_encoder_ = &response_encoder;
  new_stream->response_encoder_->getStream().addCallbacks(*new_stream);
  new_stream->buffer_limit_ = new_stream->response_encoder_->getStream().bufferLimit();
  // If the network connection is backed up, the stream should be made aware of it on creation.
  // Both HTTP/1.x and HTTP/2 codecs handle this in StreamCallbackHelper::addCallbacks_.
  ASSERT(read_callbacks_->connection().aboveHighWatermark() == false ||
         new_stream->high_watermark_count_ > 0);
  new_stream->moveIntoList(std::move(new_stream), streams_);
  return **streams_.begin();
}

void ConnectionManagerImpl::handleCodecException(const char* error) {
  ENVOY_CONN_LOG(debug, "dispatch error: {}", read_callbacks_->connection(), error);

  // In the protocol error case, we need to reset all streams now. The connection might stick around
  // long enough for a pending stream to come back and try to encode.
  resetAllStreams(StreamInfo::ResponseFlag::DownstreamProtocolError);

  // HTTP/1.1 codec has already sent a 400 response if possible. HTTP/2 codec has already sent
  // GOAWAY.
  read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWriteAndDelay);
}

Network::FilterStatus ConnectionManagerImpl::onData(Buffer::Instance& data, bool) {
  if (!codec_) {
    // Http3 codec should have been instantiated by now.
    codec_ = config_.createCodec(read_callbacks_->connection(), data, *this);
    if (codec_->protocol() == Protocol::Http2) {
      stats_.named_.downstream_cx_http2_total_.inc();
      stats_.named_.downstream_cx_http2_active_.inc();
    } else {
      ASSERT(codec_->protocol() != Protocol::Http3);
      stats_.named_.downstream_cx_http1_total_.inc();
      stats_.named_.downstream_cx_http1_active_.inc();
    }
  }

  // TODO(alyssawilk) clean this up after #8352 is well vetted.
  bool redispatch;
  do {
    redispatch = false;

    try {
      codec_->dispatch(data);
    } catch (const FrameFloodException& e) {
      // TODO(mattklein123): This is an emergency substitute for the lack of connection level
      // logging in the HCM. In a public follow up change we will add full support for connection
      // level logging in the HCM, similar to what we have in tcp_proxy. This will allow abuse
      // indicators to be stored in the connection level stream info, and then matched, sampled,
      // etc. when logged.
      const envoy::type::FractionalPercent default_value; // 0
      if (runtime_.snapshot().featureEnabled("http.connection_manager.log_flood_exception",
                                             default_value)) {
        ENVOY_CONN_LOG(warn, "downstream HTTP flood from IP '{}': {}",
                       read_callbacks_->connection(),
                       read_callbacks_->connection().remoteAddress()->asString(), e.what());
      }

      handleCodecException(e.what());
      return Network::FilterStatus::StopIteration;
    } catch (const CodecProtocolException& e) {
      stats_.named_.downstream_cx_protocol_error_.inc();
      handleCodecException(e.what());
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

      if (!streams_.empty() && streams_.front()->state_.remote_complete_) {
        read_callbacks_->connection().readDisable(true);
      }
    }
  } while (redispatch);

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus ConnectionManagerImpl::onNewConnection() {
  if (!read_callbacks_->connection().streamInfo().protocol()) {
    // For Non-QUIC traffic, continue passing data to filters.
    return Network::FilterStatus::Continue;
  }
  // Only QUIC connection's stream_info_ specifies protocol.
  Buffer::OwnedImpl dummy;
  codec_ = config_.createCodec(read_callbacks_->connection(), dummy, *this);
  ASSERT(codec_->protocol() == Protocol::Http3);
  stats_.named_.downstream_cx_http3_total_.inc();
  stats_.named_.downstream_cx_http3_active_.inc();
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
      stream.stream_info_.setResponseFlag(response_flag.value());
    }
  }
}

void ConnectionManagerImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose) {
    stats_.named_.downstream_cx_destroy_local_.inc();
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    stats_.named_.downstream_cx_destroy_remote_.inc();
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (connection_idle_timer_) {
      connection_idle_timer_->disableTimer();
      connection_idle_timer_.reset();
    }

    if (drain_timer_) {
      drain_timer_->disableTimer();
      drain_timer_.reset();
    }
  }

  if (!streams_.empty()) {
    if (event == Network::ConnectionEvent::LocalClose) {
      stats_.named_.downstream_cx_destroy_local_active_rq_.inc();
    }
    if (event == Network::ConnectionEvent::RemoteClose) {
      stats_.named_.downstream_cx_destroy_remote_active_rq_.inc();
    }

    stats_.named_.downstream_cx_destroy_active_rq_.inc();
    user_agent_.onConnectionDestroy(event, true);
    resetAllStreams(absl::nullopt);
  }
}

void ConnectionManagerImpl::onGoAway() {
  // Currently we do nothing with remote go away frames. In the future we can decide to no longer
  // push resources if applicable.
}

void ConnectionManagerImpl::onIdleTimeout() {
  ENVOY_CONN_LOG(debug, "idle timeout", read_callbacks_->connection());
  stats_.named_.downstream_cx_idle_timeout_.inc();
  if (!codec_) {
    // No need to delay close after flushing since an idle timeout has already fired. Attempt to
    // write out buffered data one last time and issue a local close if successful.
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
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
        fmt::format("invalid tracing reason, value: {}", static_cast<int32_t>(tracing_reason)));
  }
}

ConnectionManagerImpl::ActiveStream::ActiveStream(ConnectionManagerImpl& connection_manager)
    : connection_manager_(connection_manager),
      stream_id_(connection_manager.random_generator_.random()),
      request_response_timespan_(new Stats::HistogramCompletableTimespanImpl(
          connection_manager_.stats_.named_.downstream_rq_time_, connection_manager_.timeSource())),
      stream_info_(connection_manager_.codec_->protocol(), connection_manager_.timeSource()),
      upstream_options_(std::make_shared<Network::Socket::Options>()) {
  // For Server::Admin, no routeConfigProvider or SRDS route provider is used.
  ASSERT(dynamic_cast<Server::Admin*>(&connection_manager_.config_) != nullptr ||
             ((connection_manager.config_.routeConfigProvider() == nullptr &&
               connection_manager.config_.scopedRouteConfigProvider() != nullptr) ||
              (connection_manager.config_.routeConfigProvider() != nullptr &&
               connection_manager.config_.scopedRouteConfigProvider() == nullptr)),
         "Either routeConfigProvider or scopedRouteConfigProvider should be set in "
         "ConnectionManagerImpl.");
  if (connection_manager.config_.routeConfigProvider() != nullptr) {
    snapped_route_config_ = connection_manager.config_.routeConfigProvider()->config();
  } else if (connection_manager.config_.scopedRouteConfigProvider() != nullptr) {
    snapped_scoped_routes_config_ =
        connection_manager_.config_.scopedRouteConfigProvider()->config<Router::ScopedConfig>();
    ASSERT(snapped_scoped_routes_config_ != nullptr,
           "Scoped rds provider returns null for scoped routes config.");
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
  stream_info_.setDownstreamLocalAddress(
      connection_manager_.read_callbacks_->connection().localAddress());
  stream_info_.setDownstreamDirectRemoteAddress(
      connection_manager_.read_callbacks_->connection().remoteAddress());
  // Initially, the downstream remote address is the source address of the
  // downstream connection. That can change later in the request's lifecycle,
  // based on XFF processing, but setting the downstream remote address here
  // prevents surprises for logging code in edge cases.
  stream_info_.setDownstreamRemoteAddress(
      connection_manager_.read_callbacks_->connection().remoteAddress());

  stream_info_.setDownstreamSslConnection(connection_manager_.read_callbacks_->connection().ssl());

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

  stream_info_.setRequestedServerName(
      connection_manager_.read_callbacks_->connection().requestedServerName());
}

ConnectionManagerImpl::ActiveStream::~ActiveStream() {
  stream_info_.onRequestComplete();

  // A downstream disconnect can be identified for HTTP requests when the upstream returns with a 0
  // response code and when no other response flags are set.
  if (!stream_info_.hasAnyResponseFlag() && !stream_info_.responseCode()) {
    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::DownstreamConnectionTermination);
  }

  connection_manager_.stats_.named_.downstream_rq_active_.dec();
  // Refresh byte sizes of the HeaderMaps before logging.
  // TODO(asraa): Remove this when entries in HeaderMap can no longer be modified by reference and
  // HeaderMap holds an accurate internal byte size count.
  if (request_headers_ != nullptr) {
    request_headers_->refreshByteSize();
  }
  if (response_headers_ != nullptr) {
    response_headers_->refreshByteSize();
  }
  if (response_trailers_ != nullptr) {
    response_trailers_->refreshByteSize();
  }
  for (const AccessLog::InstanceSharedPtr& access_log : connection_manager_.config_.accessLogs()) {
    access_log->log(request_headers_.get(), response_headers_.get(), response_trailers_.get(),
                    stream_info_);
  }
  for (const auto& log_handler : access_log_handlers_) {
    log_handler->log(request_headers_.get(), response_headers_.get(), response_trailers_.get(),
                     stream_info_);
  }

  if (stream_info_.healthCheck()) {
    connection_manager_.config_.tracingStats().health_check_.inc();
  }

  if (active_span_) {
    Tracing::HttpTracerUtility::finalizeDownstreamSpan(
        *active_span_, request_headers_.get(), response_headers_.get(), response_trailers_.get(),
        stream_info_, *this);
  }
  if (state_.successful_upgrade_) {
    connection_manager_.stats_.named_.downstream_cx_upgrades_active_.dec();
  }

  ASSERT(state_.filter_call_state_ == 0);
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
  if (response_headers_ != nullptr) {
    // TODO(htuch): We could send trailers here with an x-envoy timeout header
    // or gRPC status code, and/or set H2 RST_STREAM error.
    connection_manager_.doEndStream(*this);
  } else {
    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout);
    sendLocalReply(request_headers_ != nullptr &&
                       Grpc::Common::hasGrpcContentType(*request_headers_),
                   Http::Code::RequestTimeout, "stream timeout", nullptr, is_head_request_,
                   absl::nullopt, StreamInfo::ResponseCodeDetails::get().StreamIdleTimeout);
  }
}

void ConnectionManagerImpl::ActiveStream::onRequestTimeout() {
  connection_manager_.stats_.named_.downstream_rq_timeout_.inc();
  sendLocalReply(request_headers_ != nullptr && Grpc::Common::hasGrpcContentType(*request_headers_),
                 Http::Code::RequestTimeout, "request timeout", nullptr, is_head_request_,
                 absl::nullopt, StreamInfo::ResponseCodeDetails::get().RequestOverallTimeout);
}

void ConnectionManagerImpl::ActiveStream::addStreamDecoderFilterWorker(
    StreamDecoderFilterSharedPtr filter, bool dual_filter) {
  ActiveStreamDecoderFilterPtr wrapper(new ActiveStreamDecoderFilter(*this, filter, dual_filter));
  filter->setDecoderFilterCallbacks(*wrapper);
  wrapper->moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void ConnectionManagerImpl::ActiveStream::addStreamEncoderFilterWorker(
    StreamEncoderFilterSharedPtr filter, bool dual_filter) {
  ActiveStreamEncoderFilterPtr wrapper(new ActiveStreamEncoderFilter(*this, filter, dual_filter));
  filter->setEncoderFilterCallbacks(*wrapper);
  wrapper->moveIntoList(std::move(wrapper), encoder_filters_);
}

void ConnectionManagerImpl::ActiveStream::addAccessLogHandler(
    AccessLog::InstanceSharedPtr handler) {
  access_log_handlers_.push_back(handler);
}

void ConnectionManagerImpl::ActiveStream::chargeStats(const HeaderMap& headers) {
  uint64_t response_code = Utility::getResponseStatus(headers);
  stream_info_.response_code_ = response_code;

  if (stream_info_.health_check_request_) {
    return;
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
//
// TODO(alyssawilk) all the calls here should be audited for order priority,
// e.g. many early returns do not currently handle connection: close properly.
void ConnectionManagerImpl::ActiveStream::decodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  request_headers_ = std::move(headers);
  // For Admin thread, we don't use routeConfigProvider or SRDS route provider.
  if (dynamic_cast<Server::Admin*>(&connection_manager_.config_) == nullptr &&
      connection_manager_.config_.scopedRouteConfigProvider() != nullptr) {
    ASSERT(snapped_route_config_ == nullptr,
           "Route config already latched to the active stream when scoped RDS is enabled.");
    // We need to snap snapped_route_config_ here as it's used in mutateRequestHeaders later.
    snapScopedRouteConfig();
  }

  if (Http::Headers::get().MethodValues.Head ==
      request_headers_->Method()->value().getStringView()) {
    is_head_request_ = true;
  }
  ENVOY_STREAM_LOG(debug, "request headers complete (end_stream={}):\n{}", *this, end_stream,
                   *request_headers_);

  // We end the decode here only if the request is header only. If we convert the request to a
  // header only, the stream will be marked as done once a subsequent decodeData/decodeTrailers is
  // called with end_stream=true.
  maybeEndDecode(end_stream);

  // Drop new requests when overloaded as soon as we have decoded the headers.
  if (connection_manager_.overload_stop_accepting_requests_ref_ ==
      Server::OverloadActionState::Active) {
    // In this one special case, do not create the filter chain. If there is a risk of memory
    // overload it is more important to avoid unnecessary allocation than to create the filters.
    state_.created_filter_chain_ = true;
    connection_manager_.stats_.named_.downstream_rq_overload_close_.inc();
    sendLocalReply(Grpc::Common::hasGrpcContentType(*request_headers_),
                   Http::Code::ServiceUnavailable, "envoy overloaded", nullptr, is_head_request_,
                   absl::nullopt, StreamInfo::ResponseCodeDetails::get().Overload);
    return;
  }

  if (!connection_manager_.config_.proxy100Continue() && request_headers_->Expect() &&
      request_headers_->Expect()->value() == Headers::get().ExpectValues._100Continue.c_str()) {
    // Note in the case Envoy is handling 100-Continue complexity, it skips the filter chain
    // and sends the 100-Continue directly to the encoder.
    chargeStats(continueHeader());
    response_encoder_->encode100ContinueHeaders(continueHeader());
    // Remove the Expect header so it won't be handled again upstream.
    request_headers_->removeExpect();
  }

  connection_manager_.user_agent_.initializeFromHeaders(
      *request_headers_, connection_manager_.stats_.prefix_, connection_manager_.stats_.scope_);

  // Make sure we are getting a codec version we support.
  Protocol protocol = connection_manager_.codec_->protocol();
  if (protocol == Protocol::Http10) {
    // Assume this is HTTP/1.0. This is fine for HTTP/0.9 but this code will also affect any
    // requests with non-standard version numbers (0.9, 1.3), basically anything which is not
    // HTTP/1.1.
    //
    // The protocol may have shifted in the HTTP/1.0 case so reset it.
    stream_info_.protocol(protocol);
    if (!connection_manager_.config_.http1Settings().accept_http_10_) {
      // Send "Upgrade Required" if HTTP/1.0 support is not explicitly configured on.
      sendLocalReply(false, Code::UpgradeRequired, "", nullptr, is_head_request_, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().LowVersion);
      return;
    } else {
      // HTTP/1.0 defaults to single-use connections. Make sure the connection
      // will be closed unless Keep-Alive is present.
      state_.saw_connection_close_ = true;
      if (request_headers_->Connection() &&
          absl::EqualsIgnoreCase(request_headers_->Connection()->value().getStringView(),
                                 Http::Headers::get().ConnectionValues.KeepAlive)) {
        state_.saw_connection_close_ = false;
      }
    }
  }

  if (!request_headers_->Host()) {
    if ((protocol == Protocol::Http10) &&
        !connection_manager_.config_.http1Settings().default_host_for_http_10_.empty()) {
      // Add a default host if configured to do so.
      request_headers_->insertHost().value(
          connection_manager_.config_.http1Settings().default_host_for_http_10_);
    } else {
      // Require host header. For HTTP/1.1 Host has already been translated to :authority.
      sendLocalReply(Grpc::Common::hasGrpcContentType(*request_headers_), Code::BadRequest, "",
                     nullptr, is_head_request_, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().MissingHost);
      return;
    }
  }

  // Currently we only support relative paths at the application layer. We expect the codec to have
  // broken the path into pieces if applicable. NOTE: Currently the HTTP/1.1 codec only does this
  // when the allow_absolute_url flag is enabled on the HCM.
  // https://tools.ietf.org/html/rfc7230#section-5.3 We also need to check for the existence of
  // :path because CONNECT does not have a path, and we don't support that currently.
  if (!request_headers_->Path() || request_headers_->Path()->value().getStringView().empty() ||
      request_headers_->Path()->value().getStringView()[0] != '/') {
    const bool has_path =
        request_headers_->Path() && !request_headers_->Path()->value().getStringView().empty();
    connection_manager_.stats_.named_.downstream_rq_non_relative_path_.inc();
    sendLocalReply(Grpc::Common::hasGrpcContentType(*request_headers_), Code::NotFound, "", nullptr,
                   is_head_request_, absl::nullopt,
                   has_path ? StreamInfo::ResponseCodeDetails::get().AbsolutePath
                            : StreamInfo::ResponseCodeDetails::get().MissingPath);
    return;
  }

  // Path sanitization should happen before any path access other than the above sanity check.
  if (!ConnectionManagerUtility::maybeNormalizePath(*request_headers_,
                                                    connection_manager_.config_)) {
    sendLocalReply(Grpc::Common::hasGrpcContentType(*request_headers_), Code::BadRequest, "",
                   nullptr, is_head_request_, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().PathNormalizationFailed);
    return;
  }

  if (protocol == Protocol::Http11 && request_headers_->Connection() &&
      absl::EqualsIgnoreCase(request_headers_->Connection()->value().getStringView(),
                             Http::Headers::get().ConnectionValues.Close)) {
    state_.saw_connection_close_ = true;
  }
  // Note: Proxy-Connection is not a standard header, but is supported here
  // since it is supported by http-parser the underlying parser for http
  // requests.
  if (protocol < Protocol::Http2 && !state_.saw_connection_close_ &&
      request_headers_->ProxyConnection() &&
      absl::EqualsIgnoreCase(request_headers_->ProxyConnection()->value().getStringView(),
                             Http::Headers::get().ConnectionValues.Close)) {
    state_.saw_connection_close_ = true;
  }

  if (!state_.is_internally_created_) { // Only sanitize headers on first pass.
    // Modify the downstream remote address depending on configuration and headers.
    stream_info_.setDownstreamRemoteAddress(ConnectionManagerUtility::mutateRequestHeaders(
        *request_headers_, connection_manager_.read_callbacks_->connection(),
        connection_manager_.config_, *snapped_route_config_, connection_manager_.random_generator_,
        connection_manager_.local_info_));
  }
  ASSERT(stream_info_.downstreamRemoteAddress() != nullptr);

  ASSERT(!cached_route_);
  refreshCachedRoute();

  if (!state_.is_internally_created_) { // Only mutate tracing headers on first pass.
    ConnectionManagerUtility::mutateTracingRequestHeader(
        *request_headers_, connection_manager_.runtime_, connection_manager_.config_,
        cached_route_.value().get());
  }

  const bool upgrade_rejected = createFilterChain() == false;

  // TODO if there are no filters when starting a filter iteration, the connection manager
  // should return 404. The current returns no response if there is no router filter.
  if (protocol == Protocol::Http11 && hasCachedRoute()) {
    if (upgrade_rejected) {
      // Do not allow upgrades if the route does not support it.
      connection_manager_.stats_.named_.downstream_rq_ws_on_non_ws_route_.inc();
      sendLocalReply(Grpc::Common::hasGrpcContentType(*request_headers_), Code::Forbidden, "",
                     nullptr, is_head_request_, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().UpgradeFailed);
      return;
    }
    // Allow non websocket requests to go through websocket enabled routes.
  }

  if (hasCachedRoute()) {
    const Router::RouteEntry* route_entry = cached_route_.value()->routeEntry();
    if (route_entry != nullptr && route_entry->idleTimeout()) {
      idle_timeout_ms_ = route_entry->idleTimeout().value();
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

  decodeHeaders(nullptr, *request_headers_, end_stream);

  // Reset it here for both global and overridden cases.
  resetIdleTimer();
}

void ConnectionManagerImpl::ActiveStream::traceRequest() {
  Tracing::Decision tracing_decision =
      Tracing::HttpTracerUtility::isTracing(stream_info_, *request_headers_);
  ConnectionManagerImpl::chargeTracingStats(tracing_decision.reason,
                                            connection_manager_.config_.tracingStats());

  active_span_ = connection_manager_.tracer().startSpan(*this, *request_headers_, stream_info_,
                                                        tracing_decision);

  if (!active_span_) {
    return;
  }

  // TODO: Need to investigate the following code based on the cached route, as may
  // be broken in the case a filter changes the route.

  // If a decorator has been defined, apply it to the active span.
  if (hasCachedRoute() && cached_route_.value()->decorator()) {
    cached_route_.value()->decorator()->apply(*active_span_);

    // Cache decorated operation.
    if (!cached_route_.value()->decorator()->getOperation().empty()) {
      decorated_operation_ = &cached_route_.value()->decorator()->getOperation();
    }
  }

  if (connection_manager_.config_.tracingConfig()->operation_name_ ==
      Tracing::OperationName::Egress) {
    // For egress (outbound) requests, pass the decorator's operation name (if defined)
    // as a request header to enable the receiving service to use it in its server span.
    if (decorated_operation_) {
      request_headers_->insertEnvoyDecoratorOperation().value(*decorated_operation_);
    }
  } else {
    const HeaderEntry* req_operation_override = request_headers_->EnvoyDecoratorOperation();

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
      request_headers_->removeEnvoyDecoratorOperation();
    }
  }
}

void ConnectionManagerImpl::ActiveStream::decodeHeaders(ActiveStreamDecoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamDecoderFilterPtr>::iterator continue_data_entry = decoder_filters_.end();

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeHeaders));
    state_.filter_call_state_ |= FilterCallState::DecodeHeaders;
    (*entry)->end_stream_ =
        decoding_headers_only_ || (end_stream && continue_data_entry == decoder_filters_.end());
    FilterHeadersStatus status = (*entry)->decodeHeaders(headers, (*entry)->end_stream_);

    ASSERT(!(status == FilterHeadersStatus::ContinueAndEndStream && (*entry)->end_stream_));
    state_.filter_call_state_ &= ~FilterCallState::DecodeHeaders;
    ENVOY_STREAM_LOG(trace, "decode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    const bool new_metadata_added = processNewlyAddedMetadata();
    // If end_stream is set in headers, and a filter adds new metadata, we need to delay end_stream
    // in headers by inserting an empty data frame with end_stream set. The empty data frame is sent
    // after the new metadata.
    if ((*entry)->end_stream_ && new_metadata_added && !buffered_request_data_) {
      Buffer::OwnedImpl empty_data("");
      ENVOY_STREAM_LOG(
          trace, "inserting an empty data frame for end_stream due metadata being added.", *this);
      // Metadata frame doesn't carry end of stream bit. We need an empty data frame to end the
      // stream.
      addDecodedData(*((*entry).get()), empty_data, true);
    }

    (*entry)->decode_headers_called_ = true;
    if (!(*entry)->commonHandleAfterHeadersCallback(status, decoding_headers_only_) &&
        std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added body.
      return;
    }

    // Here we handle the case where we have a header only request, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_request_data_ && continue_data_entry == decoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  if (continue_data_entry != decoder_filters_.end()) {
    // We use the continueDecoding() code since it will correctly handle not calling
    // decodeHeaders() again. Fake setting StopSingleIteration since the continueDecoding() code
    // expects it.
    ASSERT(buffered_request_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueDecoding();
  }

  if (end_stream) {
    disarmRequestTimeout();
  }
}

void ConnectionManagerImpl::ActiveStream::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  maybeEndDecode(end_stream);
  stream_info_.addBytesReceived(data.length());

  decodeData(nullptr, data, end_stream, FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStream::decodeData(
    ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream,
    FilterIterationStartState filter_iteration_start_state) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  resetIdleTimer();

  // If we previously decided to decode only the headers, do nothing here.
  if (decoding_headers_only_) {
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
    ENVOY_STREAM_LOG(trace, "decode data called: filter={} status={}", *this,
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

HeaderMap& ConnectionManagerImpl::ActiveStream::addDecodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!request_trailers_);

  request_trailers_ = std::make_unique<HeaderMapImpl>();
  return *request_trailers_;
}

void ConnectionManagerImpl::ActiveStream::addDecodedData(ActiveStreamDecoderFilter& filter,
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

MetadataMapVector& ConnectionManagerImpl::ActiveStream::addDecodedMetadata() {
  return *getRequestMetadataMapVector();
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(HeaderMapPtr&& trailers) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  resetIdleTimer();
  maybeEndDecode(true);
  request_trailers_ = std::move(trailers);
  decodeTrailers(nullptr, *request_trailers_);
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(ActiveStreamDecoderFilter* filter,
                                                         HeaderMap& trailers) {
  // If we previously decided to decode only the headers, do nothing here.
  if (decoding_headers_only_) {
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
    ENVOY_STREAM_LOG(trace, "decode trailers called: filter={} status={}", *this,
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
  decodeMetadata(nullptr, *metadata_map);
}

void ConnectionManagerImpl::ActiveStream::decodeMetadata(ActiveStreamDecoderFilter* filter,
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
    ENVOY_STREAM_LOG(trace, "decode metadata called: filter={} status={}, metadata: {}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status),
                     metadata_map);
  }
}

void ConnectionManagerImpl::ActiveStream::maybeEndDecode(bool end_stream) {
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;
  if (end_stream) {
    stream_info_.onLastDownstreamRxByteReceived();
    ENVOY_STREAM_LOG(debug, "request end stream", *this);
  }
}

void ConnectionManagerImpl::ActiveStream::disarmRequestTimeout() {
  if (request_timer_) {
    request_timer_->disableTimer();
  }
}

std::list<ConnectionManagerImpl::ActiveStreamEncoderFilterPtr>::iterator
ConnectionManagerImpl::ActiveStream::commonEncodePrefix(
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
ConnectionManagerImpl::ActiveStream::commonDecodePrefix(
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
  ASSERT(request_headers_ != nullptr,
         "Try to snap scoped route config when there is no request headers.");

  // NOTE: if a RDS subscription hasn't got a RouteConfiguration back, a Router::NullConfigImpl is
  // returned, in that case we let it pass.
  snapped_route_config_ = snapped_scoped_routes_config_->getRouteConfig(*request_headers_);
  if (snapped_route_config_ == nullptr) {
    ENVOY_STREAM_LOG(trace, "can't find SRDS scope.", *this);
    // TODO(stevenzzzz): Consider to pass an error message to router filter, so that it can
    // send back 404 with some more details.
    snapped_route_config_ = std::make_shared<Router::NullConfigImpl>();
  }
}

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute() {
  Router::RouteConstSharedPtr route;
  if (request_headers_ != nullptr) {
    if (dynamic_cast<Server::Admin*>(&connection_manager_.config_) == nullptr &&
        connection_manager_.config_.scopedRouteConfigProvider() != nullptr) {
      // NOTE: re-select scope as well in case the scope key header has been changed by a filter.
      snapScopedRouteConfig();
    }
    if (snapped_route_config_ != nullptr) {
      route = snapped_route_config_->route(*request_headers_, stream_info_, stream_id_);
    }
  }
  stream_info_.route_entry_ = route ? route->routeEntry() : nullptr;
  cached_route_ = std::move(route);
  if (nullptr == stream_info_.route_entry_) {
    cached_cluster_info_ = nullptr;
  } else {
    Upstream::ThreadLocalCluster* local_cluster =
        connection_manager_.cluster_manager_.get(stream_info_.route_entry_->clusterName());
    cached_cluster_info_ = (nullptr == local_cluster) ? nullptr : local_cluster->info();
  }
}

void ConnectionManagerImpl::ActiveStream::sendLocalReply(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(HeaderMap& headers)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  ENVOY_STREAM_LOG(debug, "Sending local reply with details {}", *this, details);
  ASSERT(response_headers_ == nullptr);
  // For early error handling, do a best-effort attempt to create a filter chain
  // to ensure access logging.
  if (!state_.created_filter_chain_) {
    createFilterChain();
  }
  stream_info_.setResponseCodeDetails(details);
  Utility::sendLocalReply(
      is_grpc_request,
      [this, modify_headers](HeaderMapPtr&& headers, bool end_stream) -> void {
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
        encodeData(nullptr, data, end_stream, FilterIterationStartState::CanStartFromCurrent);
      },
      state_.destroyed_, code, body, grpc_status, is_head_request);
}

void ConnectionManagerImpl::ActiveStream::encode100ContinueHeaders(
    ActiveStreamEncoderFilter* filter, HeaderMap& headers) {
  resetIdleTimer();
  ASSERT(connection_manager_.config_.proxy100Continue());
  // Make sure commonContinue continues encode100ContinueHeaders.
  has_continue_headers_ = true;

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
    ENVOY_STREAM_LOG(trace, "encode 100 continue headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfter100ContinueHeadersCallback(status)) {
      return;
    }
  }

  // Strip the T-E headers etc. Defer other header additions as well as drain-close logic to the
  // continuation headers.
  ConnectionManagerUtility::mutateResponseHeaders(headers, request_headers_.get(), EMPTY_STRING);

  // Count both the 1xx and follow-up response code in stats.
  chargeStats(headers);

  ENVOY_STREAM_LOG(debug, "encoding 100 continue headers via codec:\n{}", *this, headers);

  // Now actually encode via the codec.
  response_encoder_->encode100ContinueHeaders(headers);
}

void ConnectionManagerImpl::ActiveStream::encodeHeaders(ActiveStreamEncoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  resetIdleTimer();
  disarmRequestTimeout();

  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamEncoderFilterPtr>::iterator continue_data_entry = encoder_filters_.end();

  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeHeaders));
    state_.filter_call_state_ |= FilterCallState::EncodeHeaders;
    (*entry)->end_stream_ =
        encoding_headers_only_ || (end_stream && continue_data_entry == encoder_filters_.end());
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(headers, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeHeaders;
    ENVOY_STREAM_LOG(trace, "encode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    (*entry)->encode_headers_called_ = true;
    const auto continue_iteration =
        (*entry)->commonHandleAfterHeadersCallback(status, encoding_headers_only_);

    // If we're encoding a headers only response, then mark the local as complete. This ensures
    // that we don't attempt to reset the downstream request in doEndStream.
    if (encoding_headers_only_) {
      state_.local_complete_ = true;
    }

    if (!continue_iteration) {
      return;
    }

    // Here we handle the case where we have a header only response, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_response_data_ && continue_data_entry == encoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  // Base headers.
  connection_manager_.config_.dateProvider().setDateHeader(headers);
  // Following setReference() is safe because serverName() is constant for the life of the listener.
  const auto transformation = connection_manager_.config_.serverHeaderTransformation();
  if (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE ||
      (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::APPEND_IF_ABSENT &&
       headers.Server() == nullptr)) {
    headers.insertServer().value().setReference(connection_manager_.config_.serverName());
  }
  ConnectionManagerUtility::mutateResponseHeaders(headers, request_headers_.get(),
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

  if (connection_manager_.drain_state_ == DrainState::NotDraining && state_.saw_connection_close_) {
    ENVOY_STREAM_LOG(debug, "closing connection due to connection close header", *this);
    connection_manager_.drain_state_ = DrainState::Closing;
  }

  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.overload_disable_keepalive_ref_ == Server::OverloadActionState::Active) {
    ENVOY_STREAM_LOG(debug, "disabling keepalive due to envoy overload", *this);
    connection_manager_.drain_state_ = DrainState::Closing;
    connection_manager_.stats_.named_.downstream_cx_overload_disable_keepalive_.inc();
  }

  // If we are destroying a stream before remote is complete and the connection does not support
  // multiplexing, we should disconnect since we don't want to wait around for the request to
  // finish.
  if (!state_.remote_complete_) {
    if (connection_manager_.codec_->protocol() < Protocol::Http2) {
      connection_manager_.drain_state_ = DrainState::Closing;
    }

    connection_manager_.stats_.named_.downstream_rq_response_before_rq_complete_.inc();
  }

  if (connection_manager_.drain_state_ != DrainState::NotDraining &&
      connection_manager_.codec_->protocol() < Protocol::Http2) {
    // If the connection manager is draining send "Connection: Close" on HTTP/1.1 connections.
    // Do not do this for H2 (which drains via GOAWAY) or Upgrade (as the upgrade
    // payload is no longer HTTP/1.1)
    if (!Utility::isUpgrade(headers)) {
      headers.insertConnection().value().setReference(Headers::get().ConnectionValues.Close);
    }
  }

  if (connection_manager_.config_.tracingConfig()) {
    if (connection_manager_.config_.tracingConfig()->operation_name_ ==
        Tracing::OperationName::Ingress) {
      // For ingress (inbound) responses, if the request headers do not include a
      // decorator operation (override), then pass the decorator's operation name (if defined)
      // as a response header to enable the client service to use it in its client span.
      if (decorated_operation_) {
        headers.insertEnvoyDecoratorOperation().value(*decorated_operation_);
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

  ENVOY_STREAM_LOG(debug, "encoding headers via codec (end_stream={}):\n{}", *this,
                   encoding_headers_only_ ||
                       (end_stream && continue_data_entry == encoder_filters_.end()),
                   headers);

  // Now actually encode via the codec.
  stream_info_.onFirstDownstreamTxByteSent();
  response_encoder_->encodeHeaders(
      headers,
      encoding_headers_only_ || (end_stream && continue_data_entry == encoder_filters_.end()));
  if (continue_data_entry != encoder_filters_.end()) {
    // We use the continueEncoding() code since it will correctly handle not calling
    // encodeHeaders() again. Fake setting StopSingleIteration since the continueEncoding() code
    // expects it.
    ASSERT(buffered_response_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueEncoding();
  } else {
    // End encoding if this is a header only response, either due to a filter converting it to one
    // or due to the upstream returning headers only.
    maybeEndEncode(encoding_headers_only_ || end_stream);
  }
}

void ConnectionManagerImpl::ActiveStream::encodeMetadata(ActiveStreamEncoderFilter* filter,
                                                         MetadataMapPtr&& metadata_map_ptr) {
  resetIdleTimer();

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
    ENVOY_STREAM_LOG(trace, "encode metadata called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
  }
  // TODO(soya3129): update stats with metadata.

  // Now encode metadata via the codec.
  if (!metadata_map_ptr->empty()) {
    ENVOY_STREAM_LOG(debug, "encoding metadata via codec:\n{}", *this, *metadata_map_ptr);
    MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    response_encoder_->encodeMetadata(metadata_map_vector);
  }
}

HeaderMap& ConnectionManagerImpl::ActiveStream::addEncodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!response_trailers_);

  response_trailers_ = std::make_unique<HeaderMapImpl>();
  return *response_trailers_;
}

void ConnectionManagerImpl::ActiveStream::addEncodedData(ActiveStreamEncoderFilter& filter,
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

void ConnectionManagerImpl::ActiveStream::encodeData(
    ActiveStreamEncoderFilter* filter, Buffer::Instance& data, bool end_stream,
    FilterIterationStartState filter_iteration_start_state) {
  resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (encoding_headers_only_) {
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
    ENVOY_STREAM_LOG(trace, "encode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    if (!trailers_exists_at_start && response_trailers_ &&
        trailers_added_entry == encoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.encoder_filters_streaming_)) {
      return;
    }
  }

  ENVOY_STREAM_LOG(trace, "encoding data via codec (size={} end_stream={})", *this, data.length(),
                   end_stream);

  stream_info_.addBytesSent(data.length());

  // If trailers were adding during encodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != encoder_filters_.end()) {
    response_encoder_->encodeData(data, false);
    encodeTrailers(trailers_added_entry->get(), *response_trailers_);
  } else {
    response_encoder_->encodeData(data, end_stream);
    maybeEndEncode(end_stream);
  }
}

void ConnectionManagerImpl::ActiveStream::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                                         HeaderMap& trailers) {
  resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (encoding_headers_only_) {
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
    ENVOY_STREAM_LOG(trace, "encode trailers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  ENVOY_STREAM_LOG(debug, "encoding trailers via codec:\n{}", *this, trailers);

  response_encoder_->encodeTrailers(trailers);
  maybeEndEncode(true);
}

void ConnectionManagerImpl::ActiveStream::maybeEndEncode(bool end_stream) {
  if (end_stream) {
    ASSERT(!state_.codec_saw_local_complete_);
    state_.codec_saw_local_complete_ = true;
    stream_info_.onLastDownstreamTxByteSent();
    request_response_timespan_->complete();
    connection_manager_.doEndStream(*this);
  }
}

bool ConnectionManagerImpl::ActiveStream::processNewlyAddedMetadata() {
  if (request_metadata_map_vector_ == nullptr) {
    return false;
  }
  for (const auto& metadata_map : *getRequestMetadataMapVector()) {
    decodeMetadata(nullptr, *metadata_map);
  }
  getRequestMetadataMapVector()->clear();
  return true;
}

bool ConnectionManagerImpl::ActiveStream::handleDataIfStopAll(ActiveStreamFilterBase& filter,
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
}

void ConnectionManagerImpl::ActiveStream::onAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Disabling upstream stream due to downstream stream watermark.", *this);
  callHighWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStream::onBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Enabling upstream stream due to downstream stream watermark.", *this);
  callLowWatermarkCallbacks();
}

Tracing::OperationName ConnectionManagerImpl::ActiveStream::operationName() const {
  return connection_manager_.config_.tracingConfig()->operation_name_;
}

const std::vector<Http::LowerCaseString>&
ConnectionManagerImpl::ActiveStream::requestHeadersForTags() const {
  return connection_manager_.config_.tracingConfig()->request_headers_for_tags_;
}

bool ConnectionManagerImpl::ActiveStream::verbose() const {
  return connection_manager_.config_.tracingConfig()->verbose_;
}

uint32_t ConnectionManagerImpl::ActiveStream::maxPathTagLength() const {
  return connection_manager_.config_.tracingConfig()->max_path_tag_length_;
}

void ConnectionManagerImpl::ActiveStream::callHighWatermarkCallbacks() {
  ++high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionManagerImpl::ActiveStream::callLowWatermarkCallbacks() {
  ASSERT(high_watermark_count_ > 0);
  --high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionManagerImpl::ActiveStream::setBufferLimit(uint32_t new_limit) {
  ENVOY_STREAM_LOG(debug, "setting buffer limit to {}", *this, new_limit);
  buffer_limit_ = new_limit;
  if (buffered_request_data_) {
    buffered_request_data_->setWatermarks(buffer_limit_);
  }
  if (buffered_response_data_) {
    buffered_response_data_->setWatermarks(buffer_limit_);
  }
}

bool ConnectionManagerImpl::ActiveStream::createFilterChain() {
  if (state_.created_filter_chain_) {
    return false;
  }
  bool upgrade_rejected = false;
  auto upgrade = request_headers_ ? request_headers_->Upgrade() : nullptr;
  state_.created_filter_chain_ = true;
  if (upgrade != nullptr) {
    const Router::RouteEntry::UpgradeMap* upgrade_map = nullptr;

    // We must check if the 'cached_route_' optional is populated since this function can be called
    // early via sendLocalReply(), before the cached route is populated.
    if (hasCachedRoute() && cached_route_.value()->routeEntry()) {
      upgrade_map = &cached_route_.value()->routeEntry()->upgradeMap();
    }

    if (connection_manager_.config_.filterFactory().createUpgradeFilterChain(
            upgrade->value().getStringView(), upgrade_map, *this)) {
      state_.successful_upgrade_ = true;
      connection_manager_.stats_.named_.downstream_cx_upgrades_total_.inc();
      connection_manager_.stats_.named_.downstream_cx_upgrades_active_.inc();
      return true;
    } else {
      upgrade_rejected = true;
      // Fall through to the default filter chain. The function calling this
      // will send a local reply indicating that the upgrade failed.
    }
  }

  connection_manager_.config_.filterFactory().createFilterChain(*this);
  return !upgrade_rejected;
}

void ConnectionManagerImpl::ActiveStreamFilterBase::commonContinue() {
  // TODO(mattklein123): Raise an error if this is called during a callback.
  if (!canContinue()) {
    ENVOY_STREAM_LOG(trace, "cannot continue filter chain: filter={}", parent_,
                     static_cast<const void*>(this));
    return;
  }

  ENVOY_STREAM_LOG(trace, "continuing filter chain: filter={}", parent_,
                   static_cast<const void*>(this));
  ASSERT(!canIterate());
  // If iteration has stopped for all frame types, set iterate_from_current_filter_ to true so the
  // filter iteration starts with the current filter instead of the next one.
  if (stoppedAll()) {
    iterate_from_current_filter_ = true;
  }
  allowIteration();

  // Only resume with do100ContinueHeaders() if we've actually seen a 100-Continue.
  if (parent_.has_continue_headers_ && !continue_headers_continued_) {
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
    doHeaders(complete() && !bufferedData() && !trailers());
  }

  doMetadata();

  if (bufferedData()) {
    doData(complete() && !trailers());
  }

  if (trailers()) {
    doTrailers();
  }

  iterate_from_current_filter_ = false;
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfter100ContinueHeadersCallback(
    FilterHeadersStatus status) {
  ASSERT(parent_.has_continue_headers_);
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
    ENVOY_STREAM_LOG(debug, "converting to headers only", parent_);
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
    } else if (complete() && !trailers() && !bufferedData()) {
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
  return parent_.connection();
}

Event::Dispatcher& ConnectionManagerImpl::ActiveStreamFilterBase::dispatcher() {
  return parent_.connection_manager_.read_callbacks_->connection().dispatcher();
}

StreamInfo::StreamInfo& ConnectionManagerImpl::ActiveStreamFilterBase::streamInfo() {
  return parent_.stream_info_;
}

Tracing::Span& ConnectionManagerImpl::ActiveStreamFilterBase::activeSpan() {
  if (parent_.active_span_) {
    return *parent_.active_span_;
  } else {
    return Tracing::NullSpan::instance();
  }
}

Tracing::Config& ConnectionManagerImpl::ActiveStreamFilterBase::tracingConfig() { return parent_; }

Upstream::ClusterInfoConstSharedPtr ConnectionManagerImpl::ActiveStreamFilterBase::clusterInfo() {
  // NOTE: Refreshing route caches clusterInfo as well.
  if (!parent_.cached_route_.has_value()) {
    parent_.refreshCachedRoute();
  }

  return parent_.cached_cluster_info_.value();
}

Router::RouteConstSharedPtr ConnectionManagerImpl::ActiveStreamFilterBase::route() {
  if (!parent_.cached_route_.has_value()) {
    parent_.refreshCachedRoute();
  }

  return parent_.cached_route_.value();
}

void ConnectionManagerImpl::ActiveStreamFilterBase::clearRouteCache() {
  parent_.cached_route_ = absl::optional<Router::RouteConstSharedPtr>();
  parent_.cached_cluster_info_ = absl::optional<Upstream::ClusterInfoConstSharedPtr>();
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamDecoderFilter::createBuffer() {
  auto buffer =
      std::make_unique<Buffer::WatermarkBuffer>([this]() -> void { this->requestDataDrained(); },
                                                [this]() -> void { this->requestDataTooLarge(); });
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
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

HeaderMap& ConnectionManagerImpl::ActiveStreamDecoderFilter::addDecodedTrailers() {
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
                     ActiveStream::FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::continueDecoding() { commonContinue(); }

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encode100ContinueHeaders(
    HeaderMapPtr&& headers) {
  // If Envoy is not configured to proxy 100-Continue responses, swallow the 100 Continue
  // here. This avoids the potential situation where Envoy strips Expect: 100-Continue and sends a
  // 100-Continue, then proxies a duplicate 100 Continue from upstream.
  if (parent_.connection_manager_.config_.proxy100Continue()) {
    parent_.continue_headers_ = std::move(headers);
    parent_.encode100ContinueHeaders(nullptr, *parent_.continue_headers_);
  }
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeHeaders(HeaderMapPtr&& headers,
                                                                     bool end_stream) {
  parent_.response_headers_ = std::move(headers);
  parent_.encodeHeaders(nullptr, *parent_.response_headers_, end_stream);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeData(Buffer::Instance& data,
                                                                  bool end_stream) {
  parent_.encodeData(nullptr, data, end_stream,
                     ActiveStream::FilterIterationStartState::CanStartFromCurrent);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeTrailers(HeaderMapPtr&& trailers) {
  parent_.response_trailers_ = std::move(trailers);
  parent_.encodeTrailers(nullptr, *parent_.response_trailers_);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeMetadata(
    MetadataMapPtr&& metadata_map_ptr) {
  parent_.encodeMetadata(nullptr, std::move(metadata_map_ptr));
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::
    onDecoderFilterAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Read-disabling downstream stream due to filter callbacks.", parent_);
  parent_.response_encoder_->getStream().readDisable(true);
  parent_.connection_manager_.stats_.named_.downstream_flow_control_paused_reading_total_.inc();
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::requestDataTooLarge() {
  ENVOY_STREAM_LOG(debug, "request data too large watermark exceeded", parent_);
  if (parent_.state_.decoder_filters_streaming_) {
    onDecoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.connection_manager_.stats_.named_.downstream_rq_too_large_.inc();
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
  ENVOY_STREAM_LOG(debug, "Read-enabling downstream stream due to filter callbacks.", parent_);
  parent_.response_encoder_->getStream().readDisable(false);
  parent_.connection_manager_.stats_.named_.downstream_flow_control_resumed_reading_total_.inc();
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

bool ConnectionManagerImpl::ActiveStreamDecoderFilter::recreateStream() {
  // Because the filter's and the HCM view of if the stream has a body and if
  // the stream is complete may differ, re-check bytesReceived() to make sure
  // there was no body from the HCM's point of view.
  if (!complete() || parent_.stream_info_.bytesReceived() != 0) {
    return false;
  }
  // n.b. we do not currently change the codecs to point at the new stream
  // decoder because the decoder callbacks are complete. It would be good to
  // null out that pointer but should not be necessary.
  HeaderMapPtr request_headers(std::move(parent_.request_headers_));
  StreamEncoder* response_encoder = parent_.response_encoder_;
  parent_.response_encoder_ = nullptr;
  // This functionally deletes the stream (via deferred delete) so do not
  // reference anything beyond this point.
  parent_.connection_manager_.doEndStream(this->parent_);

  StreamDecoder& new_stream = parent_.connection_manager_.newStream(*response_encoder, true);
  new_stream.decodeHeaders(std::move(request_headers), true);
  return true;
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamEncoderFilter::createBuffer() {
  auto buffer = new Buffer::WatermarkBuffer([this]() -> void { this->responseDataDrained(); },
                                            [this]() -> void { this->responseDataTooLarge(); });
  buffer->setWatermarks(parent_.buffer_limit_);
  return Buffer::WatermarkBufferPtr{buffer};
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
void ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedData(Buffer::Instance& data,
                                                                      bool streaming) {
  return parent_.addEncodedData(*this, data, streaming);
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::injectEncodedDataToFilterChain(
    Buffer::Instance& data, bool end_stream) {
  parent_.encodeData(this, data, end_stream,
                     ActiveStream::FilterIterationStartState::CanStartFromCurrent);
}

HeaderMap& ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedTrailers() {
  return parent_.addEncodedTrailers();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedMetadata(
    MetadataMapPtr&& metadata_map_ptr) {
  return parent_.encodeMetadata(this, std::move(metadata_map_ptr));
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::
    onEncoderFilterAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Disabling upstream stream due to filter callbacks.", parent_);
  parent_.callHighWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::
    onEncoderFilterBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Enabling upstream stream due to filter callbacks.", parent_);
  parent_.callLowWatermarkCallbacks();
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::continueEncoding() { commonContinue(); }

void ConnectionManagerImpl::ActiveStreamEncoderFilter::responseDataTooLarge() {
  if (parent_.state_.encoder_filters_streaming_) {
    onEncoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.connection_manager_.stats_.named_.rs_too_large_.inc();

    // If headers have not been sent to the user, send a 500.
    if (!headers_continued_) {
      // Make sure we won't end up with nested watermark calls from the body buffer.
      parent_.state_.encoder_filters_streaming_ = true;
      allowIteration();

      parent_.stream_info_.setResponseCodeDetails(
          StreamInfo::ResponseCodeDetails::get().RequestHeadersTooLarge);
      Http::Utility::sendLocalReply(
          Grpc::Common::hasGrpcContentType(*parent_.request_headers_),
          [&](HeaderMapPtr&& response_headers, bool end_stream) -> void {
            parent_.chargeStats(*response_headers);
            parent_.response_headers_ = std::move(response_headers);
            parent_.response_encoder_->encodeHeaders(*parent_.response_headers_, end_stream);
            parent_.state_.local_complete_ = end_stream;
          },
          [&](Buffer::Instance& data, bool end_stream) -> void {
            parent_.response_encoder_->encodeData(data, end_stream);
            parent_.state_.local_complete_ = end_stream;
          },
          parent_.state_.destroyed_, Http::Code::InternalServerError,
          CodeUtility::toString(Http::Code::InternalServerError), absl::nullopt,
          parent_.is_head_request_);
      parent_.maybeEndEncode(parent_.state_.local_complete_);
    } else {
      ENVOY_STREAM_LOG(
          debug, "Resetting stream. Response data too large and headers have already been sent",
          *this);
      resetStream();
    }
  }
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::responseDataDrained() {
  onEncoderFilterBelowWriteBufferLowWatermark();
}

void ConnectionManagerImpl::ActiveStreamFilterBase::resetStream() {
  parent_.connection_manager_.stats_.named_.downstream_rq_tx_reset_.inc();
  parent_.connection_manager_.doEndStream(this->parent_);
}

uint64_t ConnectionManagerImpl::ActiveStreamFilterBase::streamId() { return parent_.stream_id_; }

} // namespace Http
} // namespace Envoy
