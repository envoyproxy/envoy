#include "source/common/http/conn_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/header_validator_errors.h"
#include "envoy/network/drain_decision.h"
#include "envoy/router/router.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/perf_tracing.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/path_utility.h"
#include "source/common/http/status.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

const absl::string_view ConnectionManagerImpl::PrematureResetTotalStreamCountKey =
    "overload.premature_reset_total_stream_count";
const absl::string_view ConnectionManagerImpl::PrematureResetMinStreamLifetimeSecondsKey =
    "overload.premature_reset_min_stream_lifetime_seconds";
// Runtime key for maximum number of requests that can be processed from a single connection per
// I/O cycle. Requests over this limit are deferred until the next I/O cycle.
const absl::string_view ConnectionManagerImpl::MaxRequestsPerIoCycle =
    "http.max_requests_per_io_cycle";
// Don't attempt to intelligently delay close: https://github.com/envoyproxy/envoy/issues/30010
const absl::string_view ConnectionManagerImpl::OptionallyDelayClose =
    "http1.optionally_delay_close";

bool requestWasConnect(const RequestHeaderMapSharedPtr& headers, Protocol protocol) {
  if (!headers) {
    return false;
  }
  if (protocol <= Protocol::Http11) {
    return HeaderUtility::isConnect(*headers);
  }
  // All HTTP/2 style upgrades were originally connect requests.
  return HeaderUtility::isConnect(*headers) || Utility::isUpgrade(*headers);
}

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

ConnectionManagerImpl::ConnectionManagerImpl(ConnectionManagerConfigSharedPtr config,
                                             const Network::DrainDecision& drain_close,
                                             Random::RandomGenerator& random_generator,
                                             Http::Context& http_context, Runtime::Loader& runtime,
                                             const LocalInfo::LocalInfo& local_info,
                                             Upstream::ClusterManager& cluster_manager,
                                             Server::OverloadManager& overload_manager,
                                             TimeSource& time_source)
    : config_(std::move(config)), stats_(config_->stats()),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          stats_.named_.downstream_cx_length_ms_, time_source)),
      drain_close_(drain_close), user_agent_(http_context.userAgentContext()),
      random_generator_(random_generator), runtime_(runtime), local_info_(local_info),
      cluster_manager_(cluster_manager), listener_stats_(config_->listenerStats()),
      overload_manager_(overload_manager),
      overload_state_(overload_manager.getThreadLocalOverloadState()),
      accept_new_http_stream_(
          overload_manager.getLoadShedPoint(Server::LoadShedPointName::get().HcmDecodeHeaders)),
      hcm_ondata_creating_codec_(
          overload_manager.getLoadShedPoint(Server::LoadShedPointName::get().HcmCodecCreation)),
      overload_stop_accepting_requests_ref_(
          overload_state_.getState(Server::OverloadActionNames::get().StopAcceptingRequests)),
      overload_disable_keepalive_ref_(
          overload_state_.getState(Server::OverloadActionNames::get().DisableHttpKeepAlive)),
      time_source_(time_source), proxy_name_(StreamInfo::ProxyStatusUtils::makeProxyName(
                                     /*node_id=*/local_info_.node().id(),
                                     /*server_name=*/config_->serverName(),
                                     /*proxy_status_config=*/config_->proxyStatusConfig())),
      max_requests_during_dispatch_(runtime_.snapshot().getInteger(
          ConnectionManagerImpl::MaxRequestsPerIoCycle, UINT32_MAX)) {
  ENVOY_LOG_ONCE_IF(
      trace, accept_new_http_stream_ == nullptr,
      "LoadShedPoint envoy.load_shed_points.http_connection_manager_decode_headers is not "
      "found. Is it configured?");
  ENVOY_LOG_ONCE_IF(trace, hcm_ondata_creating_codec_ == nullptr,
                    "LoadShedPoint envoy.load_shed_points.hcm_ondata_creating_codec is not found. "
                    "Is it configured?");
}

const ResponseHeaderMap& ConnectionManagerImpl::continueHeader() {
  static const auto headers = createHeaderMap<ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Code::Continue))}});
  return *headers;
}

void ConnectionManagerImpl::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  dispatcher_ = &callbacks.connection().dispatcher();
  if (max_requests_during_dispatch_ != UINT32_MAX) {
    deferred_request_processing_callback_ =
        dispatcher_->createSchedulableCallback([this]() -> void { onDeferredRequestProcessing(); });
  }

  stats_.named_.downstream_cx_total_.inc();
  stats_.named_.downstream_cx_active_.inc();
  if (read_callbacks_->connection().ssl()) {
    stats_.named_.downstream_cx_ssl_total_.inc();
    stats_.named_.downstream_cx_ssl_active_.inc();
  }

  read_callbacks_->connection().addConnectionCallbacks(*this);

  if (config_->addProxyProtocolConnectionState() &&
      !read_callbacks_->connection()
           .streamInfo()
           .filterState()
           ->hasData<Network::ProxyProtocolFilterState>(Network::ProxyProtocolFilterState::key())) {
    read_callbacks_->connection().streamInfo().filterState()->setData(
        Network::ProxyProtocolFilterState::key(),
        std::make_unique<Network::ProxyProtocolFilterState>(Network::ProxyProtocolData{
            read_callbacks_->connection().connectionInfoProvider().remoteAddress(),
            read_callbacks_->connection().connectionInfoProvider().localAddress()}),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
  }

  if (config_->idleTimeout()) {
    connection_idle_timer_ =
        dispatcher_->createScaledTimer(Event::ScaledTimerType::HttpDownstreamIdleConnectionTimeout,
                                       [this]() -> void { onIdleTimeout(); });
    connection_idle_timer_->enableTimer(config_->idleTimeout().value());
  }

  if (config_->maxConnectionDuration()) {
    connection_duration_timer_ =
        dispatcher_->createTimer([this]() -> void { onConnectionDurationTimeout(); });
    connection_duration_timer_->enableTimer(config_->maxConnectionDuration().value());
  }

  read_callbacks_->connection().setDelayedCloseTimeout(config_->delayedCloseTimeout());

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

void ConnectionManagerImpl::checkForDeferredClose(bool skip_delay_close) {
  Network::ConnectionCloseType close = Network::ConnectionCloseType::FlushWriteAndDelay;
  if (runtime_.snapshot().getBoolean(ConnectionManagerImpl::OptionallyDelayClose, true) &&
      skip_delay_close) {
    close = Network::ConnectionCloseType::FlushWrite;
  }
  if (drain_state_ == DrainState::Closing && streams_.empty() && !codec_->wantsToWrite()) {
    // We are closing a draining connection with no active streams and the codec has
    // nothing to write.
    doConnectionClose(close, absl::nullopt,
                      StreamInfo::LocalCloseReasons::get().DeferredCloseOnDrainedConnection);
  }
}

void ConnectionManagerImpl::doEndStream(ActiveStream& stream, bool check_for_deferred_close) {
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
  if (stream.response_encoder_ != nullptr && (!stream.filter_manager_.remoteDecodeComplete() ||
                                              !stream.state_.codec_saw_local_complete_)) {
    // Indicate local is complete at this point so that if we reset during a continuation, we don't
    // raise further data or trailers.
    ENVOY_STREAM_LOG(debug, "doEndStream() resetting stream", stream);
    // TODO(snowp): This call might not be necessary, try to clean up + remove setter function.
    stream.filter_manager_.setLocalComplete();
    stream.state_.codec_saw_local_complete_ = true;

    // Per https://tools.ietf.org/html/rfc7540#section-8.3 if there was an error
    // with the TCP connection during a CONNECT request, it should be
    // communicated via CONNECT_ERROR
    if (requestWasConnect(stream.request_headers_, codec_->protocol()) &&
        (stream.filter_manager_.streamInfo().hasResponseFlag(
             StreamInfo::CoreResponseFlag::UpstreamConnectionFailure) ||
         stream.filter_manager_.streamInfo().hasResponseFlag(
             StreamInfo::CoreResponseFlag::UpstreamConnectionTermination))) {
      stream.response_encoder_->getStream().resetStream(StreamResetReason::ConnectError);
    } else {
      if (stream.filter_manager_.streamInfo().hasResponseFlag(
              StreamInfo::CoreResponseFlag::UpstreamProtocolError)) {
        stream.response_encoder_->getStream().resetStream(StreamResetReason::ProtocolError);
      } else {
        stream.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
      }
    }
    reset_stream = true;
  }

  if (!reset_stream) {
    doDeferredStreamDestroy(stream);
  }

  if (reset_stream && codec_->protocol() < Protocol::Http2) {
    drain_state_ = DrainState::Closing;
  }

  // If HTTP/1.0 has no content length, it is framed by close and won't consider
  // the request complete until the FIN is read. Don't delay close in this case.
  bool http_10_sans_cl = (codec_->protocol() == Protocol::Http10) &&
                         (!stream.response_headers_ || !stream.response_headers_->ContentLength());
  // We also don't delay-close in the case of HTTP/1.1 where the request is
  // fully read, as there's no race condition to avoid.
  const bool connection_close =
      stream.filter_manager_.streamInfo().shouldDrainConnectionUponCompletion();
  bool request_complete = stream.filter_manager_.remoteDecodeComplete();

  if (check_for_deferred_close) {
    // Don't do delay close for HTTP/1.0 or if the request is complete.
    checkForDeferredClose(connection_close && (request_complete || http_10_sans_cl));
  }
}

void ConnectionManagerImpl::doDeferredStreamDestroy(ActiveStream& stream) {
  if (!stream.state_.is_internally_destroyed_) {
    ++closed_non_internally_destroyed_requests_;
    if (isPrematureRstStream(stream)) {
      ++number_premature_stream_resets_;
    }
  }
  if (stream.max_stream_duration_timer_ != nullptr) {
    stream.max_stream_duration_timer_->disableTimer();
    stream.max_stream_duration_timer_ = nullptr;
  }
  if (stream.stream_idle_timer_ != nullptr) {
    stream.stream_idle_timer_->disableTimer();
    stream.stream_idle_timer_ = nullptr;
  }
  stream.filter_manager_.disarmRequestTimeout();
  if (stream.request_header_timer_ != nullptr) {
    stream.request_header_timer_->disableTimer();
    stream.request_header_timer_ = nullptr;
  }
  if (stream.access_log_flush_timer_ != nullptr) {
    stream.access_log_flush_timer_->disableTimer();
    stream.access_log_flush_timer_ = nullptr;
  }

  // Only destroy the active stream if the underlying codec has notified us of
  // completion or we've internal redirect the stream.
  if (!stream.canDestroyStream()) {
    // Track that this stream is not expecting any additional calls apart from
    // codec notification.
    stream.state_.is_zombie_stream_ = true;
    return;
  }

  if (stream.response_encoder_ != nullptr) {
    stream.response_encoder_->getStream().registerCodecEventCallbacks(nullptr);
  }

  stream.completeRequest();
  stream.filter_manager_.onStreamComplete();

  // For HTTP/3, skip access logging here and add deferred logging info
  // to stream info for QuicStatsGatherer to use later.
  if (codec_ && codec_->protocol() == Protocol::Http3 &&
      // There was a downstream reset, log immediately.
      !stream.filter_manager_.sawDownstreamReset() &&
      // On recreate stream, log immediately.
      stream.response_encoder_ != nullptr &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_defer_logging_to_ack_listener")) {
    stream.deferHeadersAndTrailers();
  } else {
    // For HTTP/1 and HTTP/2, log here as usual.
    stream.filter_manager_.log(AccessLog::AccessLogType::DownstreamEnd);
  }

  stream.filter_manager_.destroyFilters();

  dispatcher_->deferredDelete(stream.removeFromList(streams_));

  // The response_encoder should never be dangling (unless we're destroying a
  // stream we are recreating) as the codec level stream will either outlive the
  // ActiveStream, or be alive in deferred deletion queue at this point.
  if (stream.response_encoder_) {
    stream.response_encoder_->getStream().removeCallbacks(stream);
  }

  if (connection_idle_timer_ && streams_.empty()) {
    connection_idle_timer_->enableTimer(config_->idleTimeout().value());
  }
  maybeDrainDueToPrematureResets();
}

RequestDecoderHandlePtr ConnectionManagerImpl::newStreamHandle(ResponseEncoder& response_encoder,
                                                               bool is_internally_created) {
  RequestDecoder& decoder = newStream(response_encoder, is_internally_created);
  return std::make_unique<ActiveStreamHandle>(static_cast<ActiveStream&>(decoder));
}

RequestDecoder& ConnectionManagerImpl::newStream(ResponseEncoder& response_encoder,
                                                 bool is_internally_created) {
  TRACE_EVENT("core", "ConnectionManagerImpl::newStream");
  if (connection_idle_timer_) {
    connection_idle_timer_->disableTimer();
  }

  ENVOY_CONN_LOG(debug, "new stream", read_callbacks_->connection());

  Buffer::BufferMemoryAccountSharedPtr downstream_stream_account =
      response_encoder.getStream().account();

  if (downstream_stream_account == nullptr) {
    // Create account, wiring the stream to use it for tracking bytes.
    // If tracking is disabled, the wiring becomes a NOP.
    auto& buffer_factory = dispatcher_->getWatermarkFactory();
    downstream_stream_account = buffer_factory.createAccount(response_encoder.getStream());
    response_encoder.getStream().setAccount(downstream_stream_account);
  }

  auto new_stream = std::make_unique<ActiveStream>(
      *this, response_encoder.getStream().bufferLimit(), std::move(downstream_stream_account));

  accumulated_requests_++;
  if (config_->maxRequestsPerConnection() > 0 &&
      accumulated_requests_ >= config_->maxRequestsPerConnection()) {
    if (codec_->protocol() < Protocol::Http2) {
      new_stream->filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(true);
      // Prevent erroneous debug log of closing due to incoming connection close header.
      drain_state_ = DrainState::Closing;
    } else if (drain_state_ == DrainState::NotDraining) {
      startDrainSequence();
    }
    ENVOY_CONN_LOG(debug, "max requests per connection reached", read_callbacks_->connection());
    stats_.named_.downstream_cx_max_requests_reached_.inc();
  }

  new_stream->state_.is_internally_created_ = is_internally_created;
  new_stream->response_encoder_ = &response_encoder;
  new_stream->response_encoder_->getStream().addCallbacks(*new_stream);
  new_stream->response_encoder_->getStream().registerCodecEventCallbacks(new_stream.get());
  new_stream->response_encoder_->getStream().setFlushTimeout(new_stream->idle_timeout_ms_);
  new_stream->streamInfo().setDownstreamBytesMeter(response_encoder.getStream().bytesMeter());
  // If the network connection is backed up, the stream should be made aware of it on creation.
  // Both HTTP/1.x and HTTP/2 codecs handle this in StreamCallbackHelper::addCallbacksHelper.
  ASSERT(read_callbacks_->connection().aboveHighWatermark() == false ||
         new_stream->filter_manager_.aboveHighWatermark());
  LinkedList::moveIntoList(std::move(new_stream), streams_);
  return **streams_.begin();
}

void ConnectionManagerImpl::handleCodecErrorImpl(absl::string_view error, absl::string_view details,
                                                 StreamInfo::CoreResponseFlag response_flag) {
  ENVOY_CONN_LOG(debug, "dispatch error: {}", read_callbacks_->connection(), error);
  read_callbacks_->connection().streamInfo().setResponseFlag(response_flag);

  // HTTP/1.1 codec has already sent a 400 response if possible. HTTP/2 codec has already sent
  // GOAWAY.
  doConnectionClose(Network::ConnectionCloseType::FlushWriteAndDelay, response_flag, details);
}

void ConnectionManagerImpl::handleCodecError(absl::string_view error) {
  handleCodecErrorImpl(error, absl::StrCat("codec_error:", StringUtil::replaceAllEmptySpace(error)),
                       StreamInfo::CoreResponseFlag::DownstreamProtocolError);
}

void ConnectionManagerImpl::handleCodecOverloadError(absl::string_view error) {
  handleCodecErrorImpl(error,
                       absl::StrCat("overload_error:", StringUtil::replaceAllEmptySpace(error)),
                       StreamInfo::CoreResponseFlag::OverloadManager);
}

void ConnectionManagerImpl::createCodec(Buffer::Instance& data) {
  ASSERT(!codec_);
  codec_ = config_->createCodec(read_callbacks_->connection(), data, *this, overload_manager_);

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
  requests_during_dispatch_count_ = 0;
  if (!codec_) {
    // Close connections if Envoy is under pressure, typically memory, before creating codec.
    if (hcm_ondata_creating_codec_ != nullptr && hcm_ondata_creating_codec_->shouldShedLoad()) {
      stats_.named_.downstream_rq_overload_close_.inc();
      handleCodecOverloadError("onData codec creation overload");
      return Network::FilterStatus::StopIteration;
    }
    // Http3 codec should have been instantiated by now.
    createCodec(data);
  }

  bool redispatch;
  do {
    redispatch = false;

    const Status status = codec_->dispatch(data);

    if (isBufferFloodError(status) || isInboundFramesWithEmptyPayloadError(status)) {
      handleCodecError(status.message());
      return Network::FilterStatus::StopIteration;
    } else if (isCodecProtocolError(status)) {
      stats_.named_.downstream_cx_protocol_error_.inc();
      handleCodecError(status.message());
      return Network::FilterStatus::StopIteration;
    } else if (isEnvoyOverloadError(status)) {
      // The other codecs aren't wired to send this status.
      ASSERT(codec_->protocol() < Protocol::Http2,
             "Expected only HTTP1.1 and below to send overload error.");
      stats_.named_.downstream_rq_overload_close_.inc();
      handleCodecOverloadError(status.message());
      return Network::FilterStatus::StopIteration;
    }
    ASSERT(status.ok());

    // Processing incoming data may release outbound data so check for closure here as well.
    checkForDeferredClose(false);

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
  // Stop iterating through network filters for QUIC. Currently QUIC connections bypass the
  // onData() interface because QUICHE already handles de-multiplexing.
  return Network::FilterStatus::StopIteration;
}

void ConnectionManagerImpl::resetAllStreams(
    absl::optional<StreamInfo::CoreResponseFlag> response_flag, absl::string_view details) {
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

    std::string details;
    if (event == Network::ConnectionEvent::RemoteClose) {
      remote_close_ = true;
      stats_.named_.downstream_cx_destroy_remote_.inc();
      details = StreamInfo::ResponseCodeDetails::get().DownstreamRemoteDisconnect;
    } else {
      absl::string_view local_close_reason = read_callbacks_->connection().localCloseReason();
      ENVOY_BUG(!local_close_reason.empty(), "Local Close Reason was not set!");
      details = fmt::format(
          fmt::runtime(StreamInfo::ResponseCodeDetails::get().DownstreamLocalDisconnect),
          StringUtil::replaceAllEmptySpace(local_close_reason));
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
    doConnectionClose(absl::nullopt, StreamInfo::CoreResponseFlag::DownstreamConnectionTermination,
                      details);
  }
}

void ConnectionManagerImpl::doConnectionClose(
    absl::optional<Network::ConnectionCloseType> close_type,
    absl::optional<StreamInfo::CoreResponseFlag> response_flag, absl::string_view details) {
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
    read_callbacks_->connection().close(close_type.value(), details);
  }
}

bool ConnectionManagerImpl::isPrematureRstStream(const ActiveStream& stream) const {
  // Check if the request was prematurely reset, by comparing its lifetime to the configured
  // threshold.
  ASSERT(!stream.state_.is_internally_destroyed_);
  absl::optional<std::chrono::nanoseconds> duration =
      stream.filter_manager_.streamInfo().currentDuration();

  // Check if request lifetime is longer than the premature reset threshold.
  if (duration) {
    const uint64_t lifetime = std::chrono::duration_cast<std::chrono::seconds>(*duration).count();
    const uint64_t min_lifetime = runtime_.snapshot().getInteger(
        ConnectionManagerImpl::PrematureResetMinStreamLifetimeSecondsKey, 1);
    if (lifetime > min_lifetime) {
      return false;
    }
  }

  // If request has completed before configured threshold, also check if the Envoy proxied the
  // response from the upstream. Requests without the response status were reset.
  // TODO(RyanTheOptimist): Possibly support half_closed_local instead.
  return !stream.filter_manager_.streamInfo().responseCode();
}

// Sends a GOAWAY if too many streams have been reset prematurely on this
// connection.
void ConnectionManagerImpl::maybeDrainDueToPrematureResets() {
  if (closed_non_internally_destroyed_requests_ == 0) {
    return;
  }

  const uint64_t limit =
      runtime_.snapshot().getInteger(ConnectionManagerImpl::PrematureResetTotalStreamCountKey, 500);

  if (closed_non_internally_destroyed_requests_ < limit) {
    // Even though the total number of streams have not reached `limit`, check if the number of bad
    // streams is high enough that even if every subsequent stream is good, the connection
    // would be closed once the limit is reached, and if so close the connection now.
    if (number_premature_stream_resets_ * 2 < limit) {
      return;
    }
  } else {
    if (number_premature_stream_resets_ * 2 < closed_non_internally_destroyed_requests_) {
      return;
    }
  }

  if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
    stats_.named_.downstream_rq_too_many_premature_resets_.inc();
    doConnectionClose(Network::ConnectionCloseType::Abort, absl::nullopt,
                      "too_many_premature_resets");
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
    doConnectionClose(Network::ConnectionCloseType::FlushWrite, absl::nullopt,
                      StreamInfo::LocalCloseReasons::get().IdleTimeoutOnConnection);
  } else if (drain_state_ == DrainState::NotDraining) {
    startDrainSequence();
  }
}

void ConnectionManagerImpl::onConnectionDurationTimeout() {
  ENVOY_CONN_LOG(debug, "max connection duration reached", read_callbacks_->connection());
  stats_.named_.downstream_cx_max_duration_reached_.inc();
  if (!codec_) {
    // Attempt to write out buffered data one last time and issue a local close if successful.
    doConnectionClose(Network::ConnectionCloseType::FlushWrite,
                      StreamInfo::CoreResponseFlag::DurationTimeout,
                      StreamInfo::ResponseCodeDetails::get().DurationTimeout);
  } else if (drain_state_ == DrainState::NotDraining) {
    startDrainSequence();
  }
}

void ConnectionManagerImpl::onDrainTimeout() {
  ASSERT(drain_state_ != DrainState::NotDraining);
  codec_->goAway();
  drain_state_ = DrainState::Closing;
  checkForDeferredClose(false);
}

void ConnectionManagerImpl::chargeTracingStats(const Tracing::Reason& tracing_reason,
                                               ConnectionManagerTracingStats& tracing_stats) {
  switch (tracing_reason) {
  case Tracing::Reason::ClientForced:
    tracing_stats.client_enabled_.inc();
    break;
  case Tracing::Reason::Sampling:
    tracing_stats.random_sampling_.inc();
    break;
  case Tracing::Reason::ServiceForced:
    tracing_stats.service_forced_.inc();
    break;
  default:
    tracing_stats.not_traceable_.inc();
    break;
  }
}

absl::optional<absl::string_view>
ConnectionManagerImpl::HttpStreamIdProviderImpl::toStringView() const {
  if (parent_.request_headers_ == nullptr) {
    return {};
  }
  ASSERT(parent_.connection_manager_.config_->requestIDExtension() != nullptr);
  return parent_.connection_manager_.config_->requestIDExtension()->get(*parent_.request_headers_);
}

absl::optional<uint64_t> ConnectionManagerImpl::HttpStreamIdProviderImpl::toInteger() const {
  if (parent_.request_headers_ == nullptr) {
    return {};
  }
  ASSERT(parent_.connection_manager_.config_->requestIDExtension() != nullptr);
  return parent_.connection_manager_.config_->requestIDExtension()->getInteger(
      *parent_.request_headers_);
}

ConnectionManagerImpl::ActiveStream::ActiveStream(ConnectionManagerImpl& connection_manager,
                                                  uint32_t buffer_limit,
                                                  Buffer::BufferMemoryAccountSharedPtr account)
    : connection_manager_(connection_manager),
      connection_manager_tracing_config_(connection_manager_.config_->tracingConfig() == nullptr
                                             ? absl::nullopt
                                             : makeOptRef<const TracingConnectionManagerConfig>(
                                                   *connection_manager_.config_->tracingConfig())),
      stream_id_(connection_manager.random_generator_.random()),
      filter_manager_(*this, *connection_manager_.dispatcher_,
                      connection_manager_.read_callbacks_->connection(), stream_id_,
                      std::move(account), connection_manager_.config_->proxy100Continue(),
                      buffer_limit, connection_manager_.config_->filterFactory(),
                      connection_manager_.config_->localReply(),
                      connection_manager_.codec_->protocol(), connection_manager_.timeSource(),
                      connection_manager_.read_callbacks_->connection().streamInfo().filterState(),
                      connection_manager_.overload_manager_),
      request_response_timespan_(new Stats::HistogramCompletableTimespanImpl(
          connection_manager_.stats_.named_.downstream_rq_time_, connection_manager_.timeSource())),
      header_validator_(
          connection_manager.config_->makeHeaderValidator(connection_manager.codec_->protocol())) {
  ASSERT(!connection_manager.config_->isRoutable() ||
             ((connection_manager.config_->routeConfigProvider() == nullptr &&
               connection_manager.config_->scopedRouteConfigProvider() != nullptr &&
               connection_manager.config_->scopeKeyBuilder().has_value()) ||
              (connection_manager.config_->routeConfigProvider() != nullptr &&
               connection_manager.config_->scopedRouteConfigProvider() == nullptr &&
               !connection_manager.config_->scopeKeyBuilder().has_value())),
         "Either routeConfigProvider or (scopedRouteConfigProvider and scopeKeyBuilder) should be "
         "set in "
         "ConnectionManagerImpl.");
  for (const AccessLog::InstanceSharedPtr& access_log : connection_manager_.config_->accessLogs()) {
    filter_manager_.addAccessLogHandler(access_log);
  }

  filter_manager_.streamInfo().setStreamIdProvider(
      std::make_shared<HttpStreamIdProviderImpl>(*this));

  filter_manager_.streamInfo().setShouldSchemeMatchUpstream(
      connection_manager.config_->shouldSchemeMatchUpstream());

  // TODO(chaoqin-li1123): can this be moved to the on demand filter?
  static const std::string route_factory = "envoy.route_config_update_requester.default";
  auto factory =
      Envoy::Config::Utility::getFactoryByName<RouteConfigUpdateRequesterFactory>(route_factory);
  if (connection_manager_.config_->isRoutable() &&
      connection_manager.config_->routeConfigProvider() != nullptr && factory) {
    route_config_update_requester_ = factory->createRouteConfigUpdateRequester(
        connection_manager.config_->routeConfigProvider());
  } else if (connection_manager_.config_->isRoutable() &&
             connection_manager.config_->scopedRouteConfigProvider() != nullptr &&
             connection_manager.config_->scopeKeyBuilder().has_value() && factory) {
    route_config_update_requester_ = factory->createRouteConfigUpdateRequester(
        connection_manager.config_->scopedRouteConfigProvider(),
        connection_manager.config_->scopeKeyBuilder());
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

  if (connection_manager_.config_->streamIdleTimeout().count()) {
    idle_timeout_ms_ = connection_manager_.config_->streamIdleTimeout();
    stream_idle_timer_ = connection_manager_.dispatcher_->createScaledTimer(
        Event::ScaledTimerType::HttpDownstreamIdleStreamTimeout,
        [this]() -> void { onIdleTimeout(); });
    resetIdleTimer();
  }

  if (connection_manager_.config_->requestTimeout().count()) {
    std::chrono::milliseconds request_timeout = connection_manager_.config_->requestTimeout();
    request_timer_ =
        connection_manager.dispatcher_->createTimer([this]() -> void { onRequestTimeout(); });
    request_timer_->enableTimer(request_timeout, this);
  }

  if (connection_manager_.config_->requestHeadersTimeout().count()) {
    std::chrono::milliseconds request_headers_timeout =
        connection_manager_.config_->requestHeadersTimeout();
    request_header_timer_ =
        connection_manager.dispatcher_->createTimer([this]() -> void { onRequestHeaderTimeout(); });
    request_header_timer_->enableTimer(request_headers_timeout, this);
  }

  const auto max_stream_duration = connection_manager_.config_->maxStreamDuration();
  if (max_stream_duration.has_value() && max_stream_duration.value().count()) {
    max_stream_duration_timer_ = connection_manager.dispatcher_->createTimer(
        [this]() -> void { onStreamMaxDurationReached(); });
    max_stream_duration_timer_->enableTimer(
        connection_manager_.config_->maxStreamDuration().value(), this);
  }

  if (connection_manager_.config_->accessLogFlushInterval().has_value()) {
    access_log_flush_timer_ = connection_manager.dispatcher_->createTimer([this]() -> void {
      // If the request is complete, we've already done the stream-end access-log, and shouldn't
      // do the periodic log.
      if (!streamInfo().requestComplete().has_value()) {
        filter_manager_.log(AccessLog::AccessLogType::DownstreamPeriodic);
        refreshAccessLogFlushTimer();
      }
      const SystemTime now = connection_manager_.timeSource().systemTime();
      // Downstream bytes meter is guaranteed to be non-null because ActiveStream and the timer
      // event are created on the same thread that sets the meter in
      // ConnectionManagerImpl::newStream.
      filter_manager_.streamInfo().getDownstreamBytesMeter()->takeDownstreamPeriodicLoggingSnapshot(
          now);
      if (auto& upstream_bytes_meter = filter_manager_.streamInfo().getUpstreamBytesMeter();
          upstream_bytes_meter != nullptr) {
        upstream_bytes_meter->takeDownstreamPeriodicLoggingSnapshot(now);
      }
    });
    refreshAccessLogFlushTimer();
  }
}

void ConnectionManagerImpl::ActiveStream::completeRequest() {
  filter_manager_.streamInfo().onRequestComplete();

  connection_manager_.stats_.named_.downstream_rq_active_.dec();
  if (filter_manager_.streamInfo().healthCheck()) {
    connection_manager_.config_->tracingStats().health_check_.inc();
  }

  if (active_span_) {
    Tracing::HttpTracerUtility::finalizeDownstreamSpan(
        *active_span_, request_headers_.get(), response_headers_.get(), response_trailers_.get(),
        filter_manager_.streamInfo(), *this);
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

  filter_manager_.streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::StreamIdleTimeout);
  sendLocalReply(Http::Utility::maybeRequestTimeoutCode(filter_manager_.remoteDecodeComplete()),
                 "stream timeout", nullptr, absl::nullopt,
                 StreamInfo::ResponseCodeDetails::get().StreamIdleTimeout);
}

void ConnectionManagerImpl::ActiveStream::onRequestTimeout() {
  connection_manager_.stats_.named_.downstream_rq_timeout_.inc();
  sendLocalReply(Http::Utility::maybeRequestTimeoutCode(filter_manager_.remoteDecodeComplete()),
                 "request timeout", nullptr, absl::nullopt,
                 StreamInfo::ResponseCodeDetails::get().RequestOverallTimeout);
}

void ConnectionManagerImpl::ActiveStream::onRequestHeaderTimeout() {
  connection_manager_.stats_.named_.downstream_rq_header_timeout_.inc();
  sendLocalReply(Http::Utility::maybeRequestTimeoutCode(filter_manager_.remoteDecodeComplete()),
                 "request header timeout", nullptr, absl::nullopt,
                 StreamInfo::ResponseCodeDetails::get().RequestHeaderTimeout);
}

void ConnectionManagerImpl::ActiveStream::onStreamMaxDurationReached() {
  ENVOY_STREAM_LOG(debug, "Stream max duration time reached", *this);
  connection_manager_.stats_.named_.downstream_rq_max_duration_reached_.inc();
  sendLocalReply(Http::Utility::maybeRequestTimeoutCode(filter_manager_.remoteDecodeComplete()),
                 "downstream duration timeout", nullptr,
                 Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                 StreamInfo::ResponseCodeDetails::get().MaxDurationTimeout);
}

void ConnectionManagerImpl::ActiveStream::chargeStats(const ResponseHeaderMap& headers) {
  uint64_t response_code = Utility::getResponseStatus(headers);
  filter_manager_.streamInfo().setResponseCode(response_code);

  if (filter_manager_.streamInfo().health_check_request_) {
    return;
  }

  // No response is sent back downstream for internal redirects, so don't charge downstream stats.
  const absl::optional<std::string>& response_code_details =
      filter_manager_.streamInfo().responseCodeDetails();
  if (response_code_details.has_value() &&
      response_code_details == Envoy::StreamInfo::ResponseCodeDetails::get().InternalRedirect) {
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

uint32_t ConnectionManagerImpl::ActiveStream::localPort() {
  auto ip = connection()->connectionInfoProvider().localAddress()->ip();
  if (ip == nullptr) {
    return 0;
  }
  return ip->port();
}

namespace {
bool streamErrorOnlyErrors(absl::string_view error_details) {
  // Pre UHV HCM did not respect stream_error_on_invalid_http_message
  // and only sent 400 for specific errors.
  // TODO(#28555): make these errors respect the stream_error_on_invalid_http_message
  return error_details == UhvResponseCodeDetail::get().FragmentInUrlPath ||
         error_details == UhvResponseCodeDetail::get().EscapedSlashesInPath ||
         error_details == UhvResponseCodeDetail::get().Percent00InPath;
}
} // namespace

bool ConnectionManagerImpl::ActiveStream::validateHeaders() {
  if (header_validator_) {
    auto validation_result = header_validator_->validateRequestHeaders(*request_headers_);
    bool failure = !validation_result.ok();
    bool redirect = false;
    bool is_grpc = Grpc::Common::hasGrpcContentType(*request_headers_);
    std::string failure_details(validation_result.details());
    if (!failure) {
      auto transformation_result = header_validator_->transformRequestHeaders(*request_headers_);
      failure = !transformation_result.ok();
      redirect = transformation_result.action() ==
                 Http::ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect;
      failure_details = std::string(transformation_result.details());
      if (redirect && !is_grpc) {
        connection_manager_.stats_.named_.downstream_rq_redirected_with_normalized_path_.inc();
      } else if (failure) {
        connection_manager_.stats_.named_.downstream_rq_failed_path_normalization_.inc();
      }
    }
    if (failure) {
      std::function<void(ResponseHeaderMap & headers)> modify_headers;
      Code response_code = failure_details == Http1ResponseCodeDetail::get().InvalidTransferEncoding
                               ? Code::NotImplemented
                               : Code::BadRequest;
      absl::optional<Grpc::Status::GrpcStatus> grpc_status;
      if (redirect && !is_grpc) {
        response_code = Code::TemporaryRedirect;
        modify_headers = [new_path = request_headers_->Path()->value().getStringView()](
                             Http::ResponseHeaderMap& response_headers) -> void {
          response_headers.addReferenceKey(Http::Headers::get().Location, new_path);
        };
      } else if (is_grpc) {
        grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal;
      }

      // H/2 codec was resetting requests that were rejected due to headers with underscores,
      // instead of sending 400. Preserving this behavior for now.
      // TODO(#24466): Make H/2 behavior consistent with H/1 and H/3.
      if (failure_details == UhvResponseCodeDetail::get().InvalidUnderscore &&
          connection_manager_.codec_->protocol() == Protocol::Http2) {
        filter_manager_.streamInfo().setResponseCodeDetails(failure_details);
        resetStream();
      } else {
        sendLocalReply(response_code, "", modify_headers, grpc_status, failure_details);
        if (!response_encoder_->streamErrorOnInvalidHttpMessage() &&
            !streamErrorOnlyErrors(failure_details)) {
          connection_manager_.handleCodecError(failure_details);
        }
      }
      return false;
    }
  }

  return true;
}

bool ConnectionManagerImpl::ActiveStream::validateTrailers() {
  if (!header_validator_) {
    return true;
  }

  auto validation_result = header_validator_->validateRequestTrailers(*request_trailers_);
  std::string failure_details(validation_result.details());
  if (validation_result.ok()) {
    auto transformation_result = header_validator_->transformRequestTrailers(*request_trailers_);
    if (transformation_result.ok()) {
      return true;
    }
    failure_details = std::string(transformation_result.details());
  }

  Code response_code = Code::BadRequest;
  absl::optional<Grpc::Status::GrpcStatus> grpc_status;
  if (Grpc::Common::hasGrpcContentType(*request_headers_)) {
    grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal;
  }

  // H/2 codec was resetting requests that were rejected due to headers with underscores,
  // instead of sending 400. Preserving this behavior for now.
  // TODO(#24466): Make H/2 behavior consistent with H/1 and H/3.
  if (failure_details == UhvResponseCodeDetail::get().InvalidUnderscore &&
      connection_manager_.codec_->protocol() == Protocol::Http2) {
    filter_manager_.streamInfo().setResponseCodeDetails(failure_details);
    resetStream();
  } else {
    // TODO(#24735): Harmonize H/2 and H/3 behavior with H/1
    if (connection_manager_.codec_->protocol() < Protocol::Http2) {
      sendLocalReply(response_code, "", nullptr, grpc_status, failure_details);
    } else {
      filter_manager_.streamInfo().setResponseCodeDetails(failure_details);
      resetStream();
    }
    if (!response_encoder_->streamErrorOnInvalidHttpMessage()) {
      connection_manager_.handleCodecError(failure_details);
    }
  }
  return false;
}

void ConnectionManagerImpl::ActiveStream::maybeEndDecode(bool end_stream) {
  // If recreateStream is called, the HCM rewinds state and may send more encodeData calls.
  if (end_stream && !filter_manager_.remoteDecodeComplete()) {
    filter_manager_.streamInfo().downstreamTiming().onLastDownstreamRxByteReceived(
        connection_manager_.dispatcher_->timeSource());
    ENVOY_STREAM_LOG(debug, "request end stream", *this);
  }
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
void ConnectionManagerImpl::ActiveStream::decodeHeaders(RequestHeaderMapSharedPtr&& headers,
                                                        bool end_stream) {
  ENVOY_STREAM_LOG(debug, "request headers complete (end_stream={}):\n{}", *this, end_stream,
                   *headers);
  // We only want to record this when reading the headers the first time, not when recreating
  // a stream.
  if (!filter_manager_.remoteDecodeComplete()) {
    filter_manager_.streamInfo().downstreamTiming().onLastDownstreamHeaderRxByteReceived(
        connection_manager_.dispatcher_->timeSource());
  }
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  request_headers_ = std::move(headers);
  filter_manager_.requestHeadersInitialized();
  if (request_header_timer_ != nullptr) {
    request_header_timer_->disableTimer();
    request_header_timer_.reset();
  }

  // Both shouldDrainConnectionUponCompletion() and is_head_request_ affect local replies: set them
  // as early as possible.
  const Protocol protocol = connection_manager_.codec_->protocol();
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http1_connection_close_header_in_redirect")) {
    if (HeaderUtility::shouldCloseConnection(protocol, *request_headers_)) {
      // Only mark the connection to be closed if the request indicates so. The connection might
      // already be marked so before this step, in which case if shouldCloseConnection() returns
      // false, the stream info value shouldn't be overridden.
      filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(true);
    }
  } else {
    filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(
        HeaderUtility::shouldCloseConnection(protocol, *request_headers_));
  }

  filter_manager_.streamInfo().protocol(protocol);

  // We end the decode here to mark that the downstream stream is complete.
  maybeEndDecode(end_stream);

  if (!validateHeaders()) {
    ENVOY_STREAM_LOG(debug, "request headers validation failed\n{}", *this, *request_headers_);
    return;
  }

  // We need to snap snapped_route_config_ here as it's used in mutateRequestHeaders later.
  if (connection_manager_.config_->isRoutable()) {
    if (connection_manager_.config_->routeConfigProvider() != nullptr) {
      snapped_route_config_ = connection_manager_.config_->routeConfigProvider()->configCast();
    } else if (connection_manager_.config_->scopedRouteConfigProvider() != nullptr &&
               connection_manager_.config_->scopeKeyBuilder().has_value()) {
      snapped_scoped_routes_config_ =
          connection_manager_.config_->scopedRouteConfigProvider()->config<Router::ScopedConfig>();
      snapScopedRouteConfig();
    }
  } else {
    snapped_route_config_ = connection_manager_.config_->routeConfigProvider()->configCast();
  }

  // Drop new requests when overloaded as soon as we have decoded the headers.
  const bool drop_request_due_to_overload =
      (connection_manager_.accept_new_http_stream_ != nullptr &&
       connection_manager_.accept_new_http_stream_->shouldShedLoad()) ||
      connection_manager_.random_generator_.bernoulli(
          connection_manager_.overload_stop_accepting_requests_ref_.value());

  if (drop_request_due_to_overload) {
    // In this one special case, do not create the filter chain. If there is a risk of memory
    // overload it is more important to avoid unnecessary allocation than to create the filters.
    filter_manager_.skipFilterChainCreation();
    connection_manager_.stats_.named_.downstream_rq_overload_close_.inc();
    sendLocalReply(
        Http::Code::ServiceUnavailable, "envoy overloaded",
        [this](Http::ResponseHeaderMap& headers) {
          if (connection_manager_.config_->appendLocalOverload()) {
            headers.addReference(Http::Headers::get().EnvoyLocalOverloaded,
                                 Http::Headers::get().EnvoyOverloadedValues.True);
          }
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().Overload);
    return;
  }

  if (!connection_manager_.config_->proxy100Continue() && request_headers_->Expect() &&
      // The Expect field-value is case-insensitive.
      // https://tools.ietf.org/html/rfc7231#section-5.1.1
      absl::EqualsIgnoreCase((request_headers_->Expect()->value().getStringView()),
                             Headers::get().ExpectValues._100Continue)) {
    // Note in the case Envoy is handling 100-Continue complexity, it skips the filter chain
    // and sends the 100-Continue directly to the encoder.
    chargeStats(continueHeader());
    response_encoder_->encode1xxHeaders(continueHeader());
    // Remove the Expect header so it won't be handled again upstream.
    request_headers_->removeExpect();
  }

  connection_manager_.user_agent_.initializeFromHeaders(*request_headers_,
                                                        connection_manager_.stats_.prefixStatName(),
                                                        connection_manager_.stats_.scope_);

  if (!request_headers_->Host()) {
    // Require host header. For HTTP/1.1 Host has already been translated to :authority.
    sendLocalReply(Code::BadRequest, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().MissingHost);
    return;
  }

  // Apply header sanity checks.
  absl::optional<std::reference_wrapper<const absl::string_view>> error =
      HeaderUtility::requestHeadersValid(*request_headers_);
  if (error != absl::nullopt) {
    sendLocalReply(Code::BadRequest, "", nullptr, absl::nullopt, error.value().get());
    if (!response_encoder_->streamErrorOnInvalidHttpMessage()) {
      connection_manager_.handleCodecError(error.value().get());
    }
    return;
  }

  // Check for the existence of the :path header for non-CONNECT requests, or present-but-empty
  // :path header for CONNECT requests. We expect the codec to have broken the path into pieces if
  // applicable. NOTE: Currently the HTTP/1.1 codec only does this when the allow_absolute_url flag
  // is enabled on the HCM.
  if ((!HeaderUtility::isConnect(*request_headers_) || request_headers_->Path()) &&
      request_headers_->getPathValue().empty()) {
    sendLocalReply(Code::NotFound, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().MissingPath);
    return;
  }

  // Rewrites the host of CONNECT-UDP requests.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_connect_udp_support") &&
      HeaderUtility::isConnectUdpRequest(*request_headers_) &&
      !HeaderUtility::rewriteAuthorityForConnectUdp(*request_headers_)) {
    sendLocalReply(Code::NotFound, "The path is incorrect for CONNECT-UDP", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().InvalidPath);
    return;
  }

  // Currently we only support relative paths at the application layer.
  if (!request_headers_->getPathValue().empty() && request_headers_->getPathValue()[0] != '/') {
    connection_manager_.stats_.named_.downstream_rq_non_relative_path_.inc();
    sendLocalReply(Code::NotFound, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().AbsolutePath);
    return;
  }

#ifndef ENVOY_ENABLE_UHV
  // In UHV mode path normalization is done in the UHV
  // Path sanitization should happen before any path access other than the above sanity check.
  const auto action =
      ConnectionManagerUtility::maybeNormalizePath(*request_headers_, *connection_manager_.config_);
  // gRPC requests are rejected if Envoy is configured to redirect post-normalization. This is
  // because gRPC clients do not support redirect.
  if (action == ConnectionManagerUtility::NormalizePathAction::Reject ||
      (action == ConnectionManagerUtility::NormalizePathAction::Redirect &&
       Grpc::Common::hasGrpcContentType(*request_headers_))) {
    connection_manager_.stats_.named_.downstream_rq_failed_path_normalization_.inc();
    sendLocalReply(Code::BadRequest, "", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().PathNormalizationFailed);
    return;
  } else if (action == ConnectionManagerUtility::NormalizePathAction::Redirect) {
    connection_manager_.stats_.named_.downstream_rq_redirected_with_normalized_path_.inc();
    sendLocalReply(
        Code::TemporaryRedirect, "",
        [new_path = request_headers_->Path()->value().getStringView()](
            Http::ResponseHeaderMap& response_headers) -> void {
          response_headers.addReferenceKey(Http::Headers::get().Location, new_path);
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().PathNormalizationFailed);
    return;
  }

  ASSERT(action == ConnectionManagerUtility::NormalizePathAction::Continue);
#endif
  auto optional_port = ConnectionManagerUtility::maybeNormalizeHost(
      *request_headers_, *connection_manager_.config_, localPort());
  if (optional_port.has_value() &&
      requestWasConnect(request_headers_, connection_manager_.codec_->protocol())) {
    filter_manager_.streamInfo().filterState()->setData(
        Router::OriginalConnectPort::key(),
        std::make_unique<Router::OriginalConnectPort>(optional_port.value()),
        StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
  }

  if (!state_.is_internally_created_) { // Only sanitize headers on first pass.
    // Modify the downstream remote address depending on configuration and headers.
    const auto mutate_result = ConnectionManagerUtility::mutateRequestHeaders(
        *request_headers_, connection_manager_.read_callbacks_->connection(),
        *connection_manager_.config_, *snapped_route_config_, connection_manager_.local_info_,
        filter_manager_.streamInfo());

    // IP detection failed, reject the request.
    if (mutate_result.reject_request.has_value()) {
      const auto& reject_request_params = mutate_result.reject_request.value();
      connection_manager_.stats_.named_.downstream_rq_rejected_via_ip_detection_.inc();
      sendLocalReply(reject_request_params.response_code, reject_request_params.body, nullptr,
                     absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().OriginalIPDetectionFailed);
      return;
    }

    filter_manager_.setDownstreamRemoteAddress(mutate_result.final_remote_address);
  }
  ASSERT(filter_manager_.streamInfo().downstreamAddressProvider().remoteAddress() != nullptr);

  ASSERT(!cached_route_);
  refreshCachedRoute();

  if (!state_.is_internally_created_) { // Only mutate tracing headers on first pass.
    filter_manager_.streamInfo().setTraceReason(
        ConnectionManagerUtility::mutateTracingRequestHeader(
            *request_headers_, connection_manager_.runtime_, *connection_manager_.config_,
            cached_route_.value().get()));
  }

  filter_manager_.streamInfo().setRequestHeaders(*request_headers_);

  const bool upgrade_rejected = filter_manager_.createFilterChain() == false;

  if (connection_manager_.config_->flushAccessLogOnNewRequest()) {
    filter_manager_.log(AccessLog::AccessLogType::DownstreamStart);
  }

  // TODO if there are no filters when starting a filter iteration, the connection manager
  // should return 404. The current returns no response if there is no router filter.
  if (hasCachedRoute()) {
    // Do not allow upgrades if the route does not support it.
    if (upgrade_rejected) {
      // While downstream servers should not send upgrade payload without the upgrade being
      // accepted, err on the side of caution and refuse to process any further requests on this
      // connection, to avoid a class of HTTP/1.1 smuggling bugs where Upgrade or CONNECT payload
      // contains a smuggled HTTP request.
      filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(true);
      connection_manager_.stats_.named_.downstream_rq_ws_on_non_ws_route_.inc();
      sendLocalReply(Code::Forbidden, "", nullptr, absl::nullopt,
                     StreamInfo::ResponseCodeDetails::get().UpgradeFailed);
      return;
    }
    // Allow non websocket requests to go through websocket enabled routes.
  }

  // Check if tracing is enabled.
  if (connection_manager_tracing_config_.has_value()) {
    traceRequest();
  }

  if (!connection_manager_.shouldDeferRequestProxyingToNextIoCycle()) {
    filter_manager_.decodeHeaders(*request_headers_, end_stream);
  } else {
    state_.deferred_to_next_io_iteration_ = true;
    state_.deferred_end_stream_ = end_stream;
  }

  // Reset it here for both global and overridden cases.
  resetIdleTimer();
}

void ConnectionManagerImpl::ActiveStream::traceRequest() {
  const Tracing::Decision tracing_decision =
      Tracing::TracerUtility::shouldTraceRequest(filter_manager_.streamInfo());
  ConnectionManagerImpl::chargeTracingStats(tracing_decision.reason,
                                            connection_manager_.config_->tracingStats());

  Tracing::HttpTraceContext trace_context(*request_headers_);
  active_span_ = connection_manager_.tracer().startSpan(
      *this, trace_context, filter_manager_.streamInfo(), tracing_decision);

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

  if (connection_manager_tracing_config_->operation_name_ == Tracing::OperationName::Egress) {
    // For egress (outbound) requests, pass the decorator's operation name (if defined and
    // propagation enabled) as a request header to enable the receiving service to use it in its
    // server span.
    if (decorated_operation_ && state_.decorated_propagate_) {
      request_headers_->setEnvoyDecoratorOperation(*decorated_operation_);
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

void ConnectionManagerImpl::ActiveStream::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  maybeEndDecode(end_stream);
  filter_manager_.streamInfo().addBytesReceived(data.length());
  if (!state_.deferred_to_next_io_iteration_) {
    filter_manager_.decodeData(data, end_stream);
  } else {
    if (!deferred_data_) {
      deferred_data_ = std::make_unique<Buffer::OwnedImpl>();
    }
    deferred_data_->move(data);
    state_.deferred_end_stream_ = end_stream;
  }
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(RequestTrailerMapPtr&& trailers) {
  ENVOY_STREAM_LOG(debug, "request trailers complete:\n{}", *this, *trailers);
  ScopeTrackerScopeState scope(this,
                               connection_manager_.read_callbacks_->connection().dispatcher());
  resetIdleTimer();

  ASSERT(!request_trailers_);
  request_trailers_ = std::move(trailers);
  if (!validateTrailers()) {
    ENVOY_STREAM_LOG(debug, "request trailers validation failed:\n{}", *this, *request_trailers_);
    return;
  }
  maybeEndDecode(true);
  if (!state_.deferred_to_next_io_iteration_) {
    filter_manager_.decodeTrailers(*request_trailers_);
  }
}

void ConnectionManagerImpl::ActiveStream::decodeMetadata(MetadataMapPtr&& metadata_map) {
  resetIdleTimer();
  if (!state_.deferred_to_next_io_iteration_) {
    // After going through filters, the ownership of metadata_map will be passed to terminal filter.
    // The terminal filter may encode metadata_map to the next hop immediately or store metadata_map
    // and encode later when connection pool is ready.
    filter_manager_.decodeMetadata(*metadata_map);
  } else {
    deferred_metadata_.push(std::move(metadata_map));
  }
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
  drain_timer_ = dispatcher_->createTimer([this]() -> void { onDrainTimeout(); });
  drain_timer_->enableTimer(config_->drainTimeout());
}

void ConnectionManagerImpl::ActiveStream::snapScopedRouteConfig() {
  // NOTE: if a RDS subscription hasn't got a RouteConfiguration back, a Router::NullConfigImpl is
  // returned, in that case we let it pass.
  auto scope_key =
      connection_manager_.config_->scopeKeyBuilder()->computeScopeKey(*request_headers_);
  snapped_route_config_ = snapped_scoped_routes_config_->getRouteConfig(scope_key);
  if (snapped_route_config_ == nullptr) {
    ENVOY_STREAM_LOG(trace, "can't find SRDS scope.", *this);
    // TODO(stevenzzzz): Consider to pass an error message to router filter, so that it can
    // send back 404 with some more details.
    snapped_route_config_ = std::make_shared<Router::NullConfigImpl>();
  }
}

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute() { refreshCachedRoute(nullptr); }

void ConnectionManagerImpl::ActiveStream::refreshDurationTimeout() {
  if (!filter_manager_.streamInfo().route() ||
      !filter_manager_.streamInfo().route()->routeEntry() || !request_headers_) {
    return;
  }
  const auto& route = filter_manager_.streamInfo().route()->routeEntry();

  auto grpc_timeout = Grpc::Common::getGrpcTimeout(*request_headers_);
  std::chrono::milliseconds timeout;
  bool disable_timer = false;

  if (!grpc_timeout || !route->grpcTimeoutHeaderMax()) {
    // Either there is no grpc-timeout header or special timeouts for it are not
    // configured. Use stream duration.
    if (route->maxStreamDuration()) {
      timeout = route->maxStreamDuration().value();
      if (timeout == std::chrono::milliseconds(0)) {
        // Explicitly configured 0 means no timeout.
        disable_timer = true;
      }
    } else {
      // Fall back to HCM config. If no HCM duration limit exists, disable
      // timers set by any prior route configuration.
      const auto max_stream_duration = connection_manager_.config_->maxStreamDuration();
      if (max_stream_duration.has_value() && max_stream_duration.value().count()) {
        timeout = max_stream_duration.value();
      } else {
        disable_timer = true;
      }
    }
  } else {
    // Start with the timeout equal to the gRPC timeout header.
    timeout = grpc_timeout.value();
    // If there's a valid cap, apply it.
    if (timeout > route->grpcTimeoutHeaderMax().value() &&
        route->grpcTimeoutHeaderMax().value() != std::chrono::milliseconds(0)) {
      timeout = route->grpcTimeoutHeaderMax().value();
    }

    // Apply the configured offset.
    if (timeout != std::chrono::milliseconds(0) && route->grpcTimeoutHeaderOffset()) {
      const auto offset = route->grpcTimeoutHeaderOffset().value();
      if (offset < timeout) {
        timeout -= offset;
      } else {
        timeout = std::chrono::milliseconds(0);
      }
    }
  }

  // Disable any existing timer if configured to do so.
  if (disable_timer) {
    if (max_stream_duration_timer_) {
      max_stream_duration_timer_->disableTimer();
      if (route->usingNewTimeouts() && Grpc::Common::isGrpcRequestHeaders(*request_headers_)) {
        request_headers_->removeGrpcTimeout();
      }
    }
    return;
  }

  // Set the header timeout before doing used-time adjustments.
  // This may result in the upstream not getting the latest results, but also
  // avoids every request getting a custom timeout based on envoy think time.
  if (route->usingNewTimeouts() && Grpc::Common::isGrpcRequestHeaders(*request_headers_)) {
    Grpc::Common::toGrpcTimeout(std::chrono::milliseconds(timeout), *request_headers_);
  }

  // See how long this stream has been alive, and adjust the timeout
  // accordingly.
  std::chrono::duration time_used = std::chrono::duration_cast<std::chrono::milliseconds>(
      connection_manager_.timeSource().monotonicTime() -
      filter_manager_.streamInfo().startTimeMonotonic());
  if (timeout > time_used) {
    timeout -= time_used;
  } else {
    timeout = std::chrono::milliseconds(0);
  }

  // Finally create (if necessary) and enable the timer.
  if (!max_stream_duration_timer_) {
    max_stream_duration_timer_ = connection_manager_.dispatcher_->createTimer(
        [this]() -> void { onStreamMaxDurationReached(); });
  }
  max_stream_duration_timer_->enableTimer(timeout);
}

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute(const Router::RouteCallback& cb) {
  // If the cached route is blocked then any attempt to clear it or refresh it
  // will be ignored.
  if (routeCacheBlocked()) {
    return;
  }

  Router::RouteConstSharedPtr route;
  if (request_headers_ != nullptr) {
    if (connection_manager_.config_->isRoutable() &&
        connection_manager_.config_->scopedRouteConfigProvider() != nullptr &&
        connection_manager_.config_->scopeKeyBuilder().has_value()) {
      // NOTE: re-select scope as well in case the scope key header has been changed by a filter.
      snapScopedRouteConfig();
    }
    if (snapped_route_config_ != nullptr) {
      route = snapped_route_config_->route(cb, *request_headers_, filter_manager_.streamInfo(),
                                           stream_id_);
    }
  }

  setRoute(route);
}

void ConnectionManagerImpl::ActiveStream::refreshCachedTracingCustomTags() {
  if (!connection_manager_tracing_config_.has_value()) {
    return;
  }
  const Tracing::CustomTagMap& conn_manager_tags = connection_manager_tracing_config_->custom_tags_;
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

// TODO(chaoqin-li1123): Make on demand vhds and on demand srds works at the same time.
void ConnectionManagerImpl::ActiveStream::requestRouteConfigUpdate(
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  ENVOY_BUG(route_config_update_requester_.has_value(),
            "RouteConfigUpdate requested but RDS not compiled into the binary. Try linking "
            "//source/common/http:rds_lib");
  if (route_config_update_requester_.has_value()) {
    (*route_config_update_requester_)
        ->requestRouteConfigUpdate(*this, route_config_updated_cb, routeConfig(),
                                   *connection_manager_.dispatcher_, *request_headers_);
  }
}

absl::optional<Router::ConfigConstSharedPtr> ConnectionManagerImpl::ActiveStream::routeConfig() {
  if (connection_manager_.config_->routeConfigProvider() != nullptr) {
    return {connection_manager_.config_->routeConfigProvider()->configCast()};
  }
  return {};
}

void ConnectionManagerImpl::ActiveStream::onLocalReply(Code code) {
  // The BadRequest error code indicates there has been a messaging error.
  if (code == Http::Code::BadRequest && connection_manager_.codec_->protocol() < Protocol::Http2 &&
      !response_encoder_->streamErrorOnInvalidHttpMessage()) {
    filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(true);
  }
}

void ConnectionManagerImpl::ActiveStream::encode1xxHeaders(ResponseHeaderMap& response_headers) {
  // Strip the T-E headers etc. Defer other header additions as well as drain-close logic to the
  // continuation headers.
  ConnectionManagerUtility::mutateResponseHeaders(
      response_headers, request_headers_.get(), *connection_manager_.config_, EMPTY_STRING,
      filter_manager_.streamInfo(), connection_manager_.proxy_name_,
      connection_manager_.clear_hop_by_hop_response_headers_);

  // Count both the 1xx and follow-up response code in stats.
  chargeStats(response_headers);

  ENVOY_STREAM_LOG(debug, "encoding 1xx continue headers via codec:\n{}", *this, response_headers);

  // Now actually encode via the codec.
  response_encoder_->encode1xxHeaders(response_headers);
}

void ConnectionManagerImpl::ActiveStream::encodeHeaders(ResponseHeaderMap& headers,
                                                        bool end_stream) {
  // Base headers.

  // We want to preserve the original date header, but we add a date header if it is absent
  if (!headers.Date()) {
    connection_manager_.config_->dateProvider().setDateHeader(headers);
  }

  // Following setReference() is safe because serverName() is constant for the life of the
  // listener.
  const auto transformation = connection_manager_.config_->serverHeaderTransformation();
  if (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE ||
      (transformation == ConnectionManagerConfig::HttpConnectionManagerProto::APPEND_IF_ABSENT &&
       headers.Server() == nullptr)) {
    headers.setReferenceServer(connection_manager_.config_->serverName());
  }
  ConnectionManagerUtility::mutateResponseHeaders(
      headers, request_headers_.get(), *connection_manager_.config_,
      connection_manager_.config_->via(), filter_manager_.streamInfo(),
      connection_manager_.proxy_name_, connection_manager_.clear_hop_by_hop_response_headers_);

  bool drain_connection_due_to_overload = false;
  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.random_generator_.bernoulli(
          connection_manager_.overload_disable_keepalive_ref_.value())) {
    ENVOY_STREAM_LOG(debug, "disabling keepalive due to envoy overload", *this);
    drain_connection_due_to_overload = true;
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
      filter_manager_.streamInfo().setShouldDrainConnectionUponCompletion(true);
    }
    // If the request came with a keep-alive and no other factor resulted in a
    // connection close header, send an explicit keep-alive header.
    if (!filter_manager_.streamInfo().shouldDrainConnectionUponCompletion()) {
      headers.setConnection(Headers::get().ConnectionValues.KeepAlive);
    }
  }

  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      filter_manager_.streamInfo().shouldDrainConnectionUponCompletion()) {
    ENVOY_STREAM_LOG(debug, "closing connection due to connection close header", *this);
    connection_manager_.drain_state_ = DrainState::Closing;
  }

  // If we are destroying a stream before remote is complete and the connection does not support
  // multiplexing, we should disconnect since we don't want to wait around for the request to
  // finish.
  if (!filter_manager_.remoteDecodeComplete()) {
    if (connection_manager_.codec_->protocol() < Protocol::Http2) {
      connection_manager_.drain_state_ = DrainState::Closing;
    }

    connection_manager_.stats_.named_.downstream_rq_response_before_rq_complete_.inc();
  }

  if (Utility::isUpgrade(headers) ||
      HeaderUtility::isConnectResponse(request_headers_.get(), *responseHeaders())) {
    state_.is_tunneling_ = true;
  }

  // Block route cache if the response headers is received and processed. Because after this
  // point, the cached route should never be updated or refreshed.
  blockRouteCache();

  if (connection_manager_.drain_state_ != DrainState::NotDraining &&
      connection_manager_.codec_->protocol() < Protocol::Http2) {
    // If the connection manager is draining send "Connection: Close" on HTTP/1.1 connections.
    // Do not do this for H2 (which drains via GOAWAY) or Upgrade or CONNECT (as the
    // payload is no longer HTTP/1.1)
    if (!state_.is_tunneling_) {
      headers.setReferenceConnection(Headers::get().ConnectionValues.Close);
    }
  }

  if (connection_manager_tracing_config_.has_value()) {
    if (connection_manager_tracing_config_->operation_name_ == Tracing::OperationName::Ingress) {
      // For ingress (inbound) responses, if the request headers do not include a
      // decorator operation (override), and the decorated operation should be
      // propagated, then pass the decorator's operation name (if defined)
      // as a response header to enable the client service to use it in its client span.
      if (decorated_operation_ && state_.decorated_propagate_) {
        headers.setEnvoyDecoratorOperation(*decorated_operation_);
      }
    } else if (connection_manager_tracing_config_->operation_name_ ==
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

  if (state_.is_tunneling_ &&
      connection_manager_.config_->flushAccessLogOnTunnelSuccessfullyEstablished()) {
    filter_manager_.log(AccessLog::AccessLogType::DownstreamTunnelSuccessfullyEstablished);
  }
  ENVOY_STREAM_LOG(debug, "encoding headers via codec (end_stream={}):\n{}", *this, end_stream,
                   headers);

  filter_manager_.streamInfo().downstreamTiming().onFirstDownstreamTxByteSent(
      connection_manager_.time_source_);

  if (header_validator_) {
    auto result = header_validator_->transformResponseHeaders(headers);
    if (!result.status.ok()) {
      // It is possible that the header map is invalid if an encoder filter makes invalid
      // modifications
      // TODO(yanavlasov): add handling for this case.
    } else if (result.new_headers) {
      response_encoder_->encodeHeaders(*result.new_headers, end_stream);
      return;
    }
  }

  // Now actually encode via the codec.
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

void ConnectionManagerImpl::ActiveStream::encodeMetadata(MetadataMapPtr&& metadata) {
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.emplace_back(std::move(metadata));
  ENVOY_STREAM_LOG(debug, "encoding metadata via codec:\n{}", *this, metadata_map_vector);
  response_encoder_->encodeMetadata(metadata_map_vector);
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

void ConnectionManagerImpl::ActiveStream::onResetStream(StreamResetReason reset_reason,
                                                        absl::string_view) {
  // NOTE: This function gets called in all of the following cases:
  //       1) We TX an app level reset
  //       2) The codec TX a codec level reset
  //       3) The codec RX a reset
  //       4) The overload manager reset the stream
  //       If we need to differentiate we need to do it inside the codec. Can start with this.
  const absl::string_view encoder_details = response_encoder_->getStream().responseDetails();
  ENVOY_STREAM_LOG(debug, "stream reset: reset reason: {}, response details: {}", *this,
                   Http::Utility::resetReasonToString(reset_reason),
                   encoder_details.empty() ? absl::string_view{"-"} : encoder_details);
  connection_manager_.stats_.named_.downstream_rq_rx_reset_.inc();
  state_.on_reset_stream_called_ = true;

  // If the codec sets its responseDetails() for a reason other than peer reset, set a
  // DownstreamProtocolError. Either way, propagate details.
  if (!encoder_details.empty() && reset_reason == StreamResetReason::LocalReset) {
    filter_manager_.streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::DownstreamProtocolError);
  }
  if (!encoder_details.empty()) {
    if (reset_reason == StreamResetReason::ConnectError ||
        reset_reason == StreamResetReason::RemoteReset ||
        reset_reason == StreamResetReason::RemoteRefusedStreamReset) {
      filter_manager_.streamInfo().setResponseFlag(
          StreamInfo::CoreResponseFlag::DownstreamRemoteReset);
    }
    filter_manager_.streamInfo().setResponseCodeDetails(encoder_details);
  }

  // Check if we're in the overload manager reset case.
  // encoder_details should be empty in this case as we don't have a codec error.
  if (encoder_details.empty() && reset_reason == StreamResetReason::OverloadManager) {
    filter_manager_.streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager);
    filter_manager_.streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().Overload);
  }
  filter_manager_.onDownstreamReset();
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

void ConnectionManagerImpl::ActiveStream::onCodecEncodeComplete() {
  ASSERT(!state_.codec_encode_complete_);
  ENVOY_STREAM_LOG(debug, "Codec completed encoding stream.", *this);
  state_.codec_encode_complete_ = true;

  // Update timing
  filter_manager_.streamInfo().downstreamTiming().onLastDownstreamTxByteSent(
      connection_manager_.time_source_);
  request_response_timespan_->complete();

  // Only reap stream once.
  if (state_.is_zombie_stream_) {
    connection_manager_.doDeferredStreamDestroy(*this);
  }
}

void ConnectionManagerImpl::ActiveStream::onCodecLowLevelReset() {
  ASSERT(!state_.codec_encode_complete_);
  state_.on_reset_stream_called_ = true;
  ENVOY_STREAM_LOG(debug, "Codec timed out flushing stream", *this);

  // TODO(kbaichoo): Update streamInfo to account for the reset.

  // Only reap stream once.
  if (state_.is_zombie_stream_) {
    connection_manager_.doDeferredStreamDestroy(*this);
  }
}

Tracing::OperationName ConnectionManagerImpl::ActiveStream::operationName() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->operation_name_;
}

const Tracing::CustomTagMap* ConnectionManagerImpl::ActiveStream::customTags() const {
  return tracing_custom_tags_.get();
}

bool ConnectionManagerImpl::ActiveStream::verbose() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->verbose_;
}

uint32_t ConnectionManagerImpl::ActiveStream::maxPathTagLength() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->max_path_tag_length_;
}

bool ConnectionManagerImpl::ActiveStream::spawnUpstreamSpan() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->spawn_upstream_span_;
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

OptRef<const Tracing::Config> ConnectionManagerImpl::ActiveStream::tracingConfig() const {
  if (connection_manager_tracing_config_.has_value()) {
    return makeOptRef<const Tracing::Config>(*this);
  }
  return {};
}

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

/**
 * Sets the cached route to the RouteConstSharedPtr argument passed in. Handles setting the
 * cached_route_/cached_cluster_info_ ActiveStream attributes, the FilterManager streamInfo, tracing
 * tags, and timeouts.
 *
 * Declared as a StreamFilterCallbacks member function for filters to call directly, but also
 * functions as a helper to refreshCachedRoute(const Router::RouteCallback& cb).
 */
void ConnectionManagerImpl::ActiveStream::setRoute(Router::RouteConstSharedPtr route) {
  // If the cached route is blocked then any attempt to clear it or refresh it
  // will be ignored.
  // setRoute() may be called directly by the interface of DownstreamStreamFilterCallbacks,
  // so check for routeCacheBlocked() here again.
  if (routeCacheBlocked()) {
    return;
  }

  // Update the cached route.
  setCachedRoute({route});
  // Update the cached cluster info based on the new route.
  if (nullptr == route || nullptr == route->routeEntry()) {
    cached_cluster_info_ = nullptr;
  } else {
    auto* cluster = connection_manager_.cluster_manager_.getThreadLocalCluster(
        route->routeEntry()->clusterName());
    cached_cluster_info_ = (nullptr == cluster) ? nullptr : cluster->info();
  }

  // Update route and cluster info in the filter manager's stream info.
  filter_manager_.streamInfo().route_ = std::move(route); // Now can move route here safely.
  filter_manager_.streamInfo().setUpstreamClusterInfo(cached_cluster_info_.value());

  refreshCachedTracingCustomTags();
  refreshDurationTimeout();
  refreshIdleTimeout();
}

void ConnectionManagerImpl::ActiveStream::refreshIdleTimeout() {
  if (hasCachedRoute()) {
    const Router::RouteEntry* route_entry = cached_route_.value()->routeEntry();
    if (route_entry != nullptr && route_entry->idleTimeout()) {
      idle_timeout_ms_ = route_entry->idleTimeout().value();
      response_encoder_->getStream().setFlushTimeout(idle_timeout_ms_);
      if (idle_timeout_ms_.count()) {
        // If we have a route-level idle timeout but no global stream idle timeout, create a timer.
        if (stream_idle_timer_ == nullptr) {
          stream_idle_timer_ = connection_manager_.dispatcher_->createScaledTimer(
              Event::ScaledTimerType::HttpDownstreamIdleStreamTimeout,
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
}

void ConnectionManagerImpl::ActiveStream::refreshAccessLogFlushTimer() {
  if (connection_manager_.config_->accessLogFlushInterval().has_value()) {
    access_log_flush_timer_->enableTimer(
        connection_manager_.config_->accessLogFlushInterval().value(), this);
  }
}

void ConnectionManagerImpl::ActiveStream::clearRouteCache() {
  // If the cached route is blocked then any attempt to clear it or refresh it
  // will be ignored.
  if (routeCacheBlocked()) {
    return;
  }

  setCachedRoute({});

  cached_cluster_info_ = absl::optional<Upstream::ClusterInfoConstSharedPtr>();
  if (tracing_custom_tags_) {
    tracing_custom_tags_->clear();
  }
}

void ConnectionManagerImpl::ActiveStream::setCachedRoute(
    absl::optional<Router::RouteConstSharedPtr>&& route) {
  if (hasCachedRoute()) {
    // The configuration of the route may be referenced by some filters.
    // Cache the route to avoid it being destroyed before the stream is destroyed.
    cleared_cached_routes_.emplace_back(std::move(cached_route_.value()));
  }
  cached_route_ = std::move(route);
}

void ConnectionManagerImpl::ActiveStream::blockRouteCache() {
  route_cache_blocked_ = true;
  // Clear the snapped route configuration because it is unnecessary to keep it.
  snapped_route_config_.reset();
  snapped_scoped_routes_config_.reset();
}

void ConnectionManagerImpl::ActiveStream::onRequestDataTooLarge() {
  connection_manager_.stats_.named_.downstream_rq_too_large_.inc();
}

void ConnectionManagerImpl::ActiveStream::recreateStream(
    StreamInfo::FilterStateSharedPtr filter_state) {
  ResponseEncoder* response_encoder = response_encoder_;
  response_encoder_ = nullptr;

  Buffer::InstancePtr request_data = std::make_unique<Buffer::OwnedImpl>();
  const auto& buffered_request_data = filter_manager_.bufferedRequestData();
  const bool proxy_body = buffered_request_data != nullptr && buffered_request_data->length() > 0;
  if (proxy_body) {
    request_data->move(*buffered_request_data);
  }

  response_encoder->getStream().removeCallbacks(*this);

  // This functionally deletes the stream (via deferred delete) so do not
  // reference anything beyond this point.
  // Make sure to not check for deferred close as we'll be immediately creating a new stream.
  state_.is_internally_destroyed_ = true;
  connection_manager_.doEndStream(*this, /*check_for_deferred_close*/ false);

  RequestDecoder& new_stream = connection_manager_.newStream(*response_encoder, true);

  // Set the new RequestDecoder on the ResponseEncoder. Even though all of the decoder callbacks
  // have already been called at this point, the encoder still needs the new decoder for deferred
  // logging in some cases.
  // This doesn't currently work for HTTP/1 as the H/1 ResponseEncoder doesn't hold the active
  // stream's pointer to the RequestDecoder.
  response_encoder->setRequestDecoder(new_stream);
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

  // Make sure that relevant information makes it from the original stream info
  // to the new one. Generally this should consist of all downstream related
  // data, and not include upstream related data.
  (*connection_manager_.streams_.begin())
      ->filter_manager_.streamInfo()
      .setFromForRecreateStream(filter_manager_.streamInfo());
  new_stream.decodeHeaders(std::move(request_headers_), !proxy_body);
  if (proxy_body) {
    // This functionality is currently only used for internal redirects, which the router only
    // allows if the full request has been read (end_stream = true) so we don't need to handle the
    // case of upstream sending an early response mid-request.
    new_stream.decodeData(*request_data, true);
  }
}

Http1StreamEncoderOptionsOptRef ConnectionManagerImpl::ActiveStream::http1StreamEncoderOptions() {
  return response_encoder_->http1StreamEncoderOptions();
}

void ConnectionManagerImpl::ActiveStream::onResponseDataTooLarge() {
  connection_manager_.stats_.named_.rs_too_large_.inc();
}

void ConnectionManagerImpl::ActiveStream::resetStream(Http::StreamResetReason, absl::string_view) {
  connection_manager_.stats_.named_.downstream_rq_tx_reset_.inc();
  connection_manager_.doEndStream(*this);
}

bool ConnectionManagerImpl::ActiveStream::onDeferredRequestProcessing() {
  // TODO(yanavlasov): Merge this with the filter manager continueIteration() method
  if (!state_.deferred_to_next_io_iteration_) {
    return false;
  }
  state_.deferred_to_next_io_iteration_ = false;
  bool end_stream = state_.deferred_end_stream_ && deferred_data_ == nullptr &&
                    request_trailers_ == nullptr && deferred_metadata_.empty();
  filter_manager_.decodeHeaders(*request_headers_, end_stream);
  if (end_stream) {
    return true;
  }
  // Send metadata before data, as data may have an associated end_stream.
  while (!deferred_metadata_.empty()) {
    MetadataMapPtr& metadata = deferred_metadata_.front();
    filter_manager_.decodeMetadata(*metadata);
    deferred_metadata_.pop();
  }
  // Filter manager will return early from decodeData and decodeTrailers if
  // request has completed.
  if (deferred_data_ != nullptr) {
    end_stream = state_.deferred_end_stream_ && request_trailers_ == nullptr;
    filter_manager_.decodeData(*deferred_data_, end_stream);
  }
  if (request_trailers_ != nullptr) {
    filter_manager_.decodeTrailers(*request_trailers_);
  }
  return true;
}

bool ConnectionManagerImpl::shouldDeferRequestProxyingToNextIoCycle() {
  // Do not defer this stream if stream deferral is disabled
  if (deferred_request_processing_callback_ == nullptr) {
    return false;
  }
  // Defer this stream if there are already deferred streams, so they are not
  // processed out of order
  if (deferred_request_processing_callback_->enabled()) {
    return true;
  }
  ++requests_during_dispatch_count_;
  bool defer = requests_during_dispatch_count_ > max_requests_during_dispatch_;
  if (defer) {
    deferred_request_processing_callback_->scheduleCallbackNextIteration();
  }
  return defer;
}

void ConnectionManagerImpl::onDeferredRequestProcessing() {
  if (streams_.empty()) {
    return;
  }
  requests_during_dispatch_count_ = 1; // 1 stream is always let through
  // Streams are inserted at the head of the list. As such process deferred
  // streams in the reverse order.
  auto reverse_iter = std::prev(streams_.end());
  bool at_first_element = false;
  do {
    at_first_element = reverse_iter == streams_.begin();
    // Move the iterator to the previous item in case the `onDeferredRequestProcessing` call removes
    // the stream from the list.
    auto previous_element = std::prev(reverse_iter);
    bool was_deferred = (*reverse_iter)->onDeferredRequestProcessing();
    if (was_deferred && shouldDeferRequestProxyingToNextIoCycle()) {
      break;
    }
    reverse_iter = previous_element;
    // TODO(yanavlasov): see if `rend` can be used.
  } while (!at_first_element);
}

} // namespace Http
} // namespace Envoy
