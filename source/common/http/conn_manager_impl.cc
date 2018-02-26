#include "common/http/conn_manager_impl.h"

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/drain_decision.h"
#include "envoy/router/router.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/tracing/http_tracer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/conn_manager_utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Http {

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
                                             Tracing::HttpTracer& tracer, Runtime::Loader& runtime,
                                             const LocalInfo::LocalInfo& local_info,
                                             Upstream::ClusterManager& cluster_manager)
    : config_(config), stats_(config_.stats()),
      conn_length_(new Stats::Timespan(stats_.named_.downstream_cx_length_ms_)),
      drain_close_(drain_close), random_generator_(random_generator), tracer_(tracer),
      runtime_(runtime), local_info_(local_info), cluster_manager_(cluster_manager),
      listener_stats_(config_.listenerStats()) {}

const HeaderMapImpl& ConnectionManagerImpl::continueHeader() {
  CONSTRUCT_ON_FIRST_USE(HeaderMapImpl,
                         Http::HeaderMapImpl{{Http::Headers::get().Status,
                                              std::to_string(enumToInt(Code::Continue))}});
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

  if (config_.idleTimeout().valid()) {
    idle_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onIdleTimeout(); });
    idle_timer_->enableTimer(config_.idleTimeout().value());
  }

  read_callbacks_->connection().setConnectionStats(
      {stats_.named_.downstream_cx_rx_bytes_total_, stats_.named_.downstream_cx_rx_bytes_buffered_,
       stats_.named_.downstream_cx_tx_bytes_total_, stats_.named_.downstream_cx_tx_bytes_buffered_,
       nullptr});
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
    } else {
      if (isWebSocketConnection()) {
        stats_.named_.downstream_cx_websocket_active_.dec();
      } else {
        stats_.named_.downstream_cx_http1_active_.dec();
      }
    }
  }

  conn_length_->complete();
  user_agent_.completeConnectionLength(*conn_length_);
}

void ConnectionManagerImpl::checkForDeferredClose() {
  if (drain_state_ == DrainState::Closing && streams_.empty() && !codec_->wantsToWrite()) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
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
  if (!stream.state_.remote_complete_ || !stream.state_.local_complete_) {
    // Indicate local is complete at this point so that if we reset during a continuation, we don't
    // raise further data or trailers.
    stream.state_.local_complete_ = true;
    stream.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
    reset_stream = true;
  }

  if (!reset_stream) {
    doDeferredStreamDestroy(stream);
  }

  if (reset_stream && codec_->protocol() != Protocol::Http2) {
    drain_state_ = DrainState::Closing;
  }

  checkForDeferredClose();

  // Reading may have been disabled for the non-multiplexing case, so enable it again.
  // Also be sure to unwind any read-disable done by the prior downstream
  // connection.
  if (drain_state_ != DrainState::Closing && codec_->protocol() != Protocol::Http2) {
    while (!read_callbacks_->connection().readEnabled()) {
      read_callbacks_->connection().readDisable(false);
    }
  }

  if (idle_timer_ && streams_.empty()) {
    idle_timer_->enableTimer(config_.idleTimeout().value());
  }
}

void ConnectionManagerImpl::doDeferredStreamDestroy(ActiveStream& stream) {
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
}

StreamDecoder& ConnectionManagerImpl::newStream(StreamEncoder& response_encoder) {
  if (idle_timer_) {
    idle_timer_->disableTimer();
  }

  ENVOY_CONN_LOG(debug, "new stream", read_callbacks_->connection());
  ActiveStreamPtr new_stream(new ActiveStream(*this));
  new_stream->response_encoder_ = &response_encoder;
  new_stream->response_encoder_->getStream().addCallbacks(*new_stream);
  new_stream->buffer_limit_ = new_stream->response_encoder_->getStream().bufferLimit();
  config_.filterFactory().createFilterChain(*new_stream);
  // Make sure new streams are apprised that the underlying connection is blocked.
  if (read_callbacks_->connection().aboveHighWatermark()) {
    new_stream->callHighWatermarkCallbacks();
  }
  new_stream->moveIntoList(std::move(new_stream), streams_);
  return **streams_.begin();
}

Network::FilterStatus ConnectionManagerImpl::onData(Buffer::Instance& data, bool end_stream) {
  // Send the data through WebSocket handlers if this connection is a
  // WebSocket connection. N.B. The first request from the client to Envoy
  // will still be processed as a normal HTTP/1.1 request, where Envoy will
  // detect the WebSocket upgrade and establish a connection to the
  // upstream.
  if (isWebSocketConnection()) {
    return ws_connection_->onData(data, end_stream);
  }

  if (!codec_) {
    codec_ = config_.createCodec(read_callbacks_->connection(), data, *this);
    if (codec_->protocol() == Protocol::Http2) {
      stats_.named_.downstream_cx_http2_total_.inc();
      stats_.named_.downstream_cx_http2_active_.inc();
    } else {
      stats_.named_.downstream_cx_http1_total_.inc();
      stats_.named_.downstream_cx_http1_active_.inc();
    }
  }

  bool redispatch;
  do {
    redispatch = false;

    try {
      codec_->dispatch(data);
    } catch (const CodecProtocolException& e) {
      // HTTP/1.1 codec has already sent a 400 response if possible. HTTP/2 codec has already sent
      // GOAWAY.
      ENVOY_CONN_LOG(debug, "dispatch error: {}", read_callbacks_->connection(), e.what());
      stats_.named_.downstream_cx_protocol_error_.inc();

      // In the protocol error case, we need to reset all streams now. Since we do a flush write,
      // the connection might stick around long enough for a pending stream to come back and try
      // to encode.
      resetAllStreams();

      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration;
    }

    // Processing incoming data may release outbound data so check for closure here as well.
    checkForDeferredClose();

    // The HTTP/1 codec will pause dispatch after a single message is complete. We want to
    // either redispatch if there are no streams and we have more data. If we have a single
    // complete non-WebSocket stream but have not responded yet we will pause socket reads
    // to apply back pressure.
    if (codec_->protocol() != Protocol::Http2) {
      if (read_callbacks_->connection().state() == Network::Connection::State::Open &&
          data.length() > 0 && streams_.empty()) {
        redispatch = true;
      }

      if (!streams_.empty() && streams_.front()->state_.remote_complete_ &&
          !isWebSocketConnection()) {
        read_callbacks_->connection().readDisable(true);
      }
    }
  } while (redispatch);

  return Network::FilterStatus::StopIteration;
}

void ConnectionManagerImpl::resetAllStreams() {
  while (!streams_.empty()) {
    // Mimic a downstream reset in this case.
    streams_.front()->onResetStream(StreamResetReason::ConnectionTermination);
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
    if (idle_timer_) {
      idle_timer_->disableTimer();
      idle_timer_.reset();
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
    resetAllStreams();
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
      snapped_route_config_(connection_manager.config_.routeConfigProvider().config()),
      stream_id_(connection_manager.random_generator_.random()),
      request_timer_(new Stats::Timespan(connection_manager_.stats_.named_.downstream_rq_time_)),
      request_info_(connection_manager_.codec_->protocol()) {
  connection_manager_.stats_.named_.downstream_rq_total_.inc();
  connection_manager_.stats_.named_.downstream_rq_active_.inc();
  if (connection_manager_.codec_->protocol() == Protocol::Http2) {
    connection_manager_.stats_.named_.downstream_rq_http2_total_.inc();
  } else {
    connection_manager_.stats_.named_.downstream_rq_http1_total_.inc();
  }
}

ConnectionManagerImpl::ActiveStream::~ActiveStream() {
  connection_manager_.stats_.named_.downstream_rq_active_.dec();
  for (const AccessLog::InstanceSharedPtr& access_log : connection_manager_.config_.accessLogs()) {
    access_log->log(request_headers_.get(), response_headers_.get(), request_info_);
  }
  for (const auto& log_handler : access_log_handlers_) {
    log_handler->log(request_headers_.get(), response_headers_.get(), request_info_);
  }

  if (request_info_.healthCheck()) {
    connection_manager_.config_.tracingStats().health_check_.inc();
  } else if (active_span_) {
    Tracing::HttpTracerUtility::finalizeSpan(*active_span_, request_headers_.get(), request_info_,
                                             *this);
  }

  ASSERT(state_.filter_call_state_ == 0);
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
  wrapper->moveIntoListBack(std::move(wrapper), encoder_filters_);
}

void ConnectionManagerImpl::ActiveStream::addAccessLogHandler(
    AccessLog::InstanceSharedPtr handler) {
  access_log_handlers_.push_back(handler);
}

void ConnectionManagerImpl::ActiveStream::chargeStats(const HeaderMap& headers) {
  uint64_t response_code = Utility::getResponseStatus(headers);
  request_info_.response_code_.value(response_code);

  if (request_info_.hc_request_) {
    return;
  }

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

uint64_t ConnectionManagerImpl::ActiveStream::connectionId() {
  return connection_manager_.read_callbacks_->connection().id();
}

const Network::Connection* ConnectionManagerImpl::ActiveStream::connection() {
  return &connection_manager_.read_callbacks_->connection();
}

Ssl::Connection* ConnectionManagerImpl::ActiveStream::ssl() {
  return connection_manager_.read_callbacks_->connection().ssl();
}

void ConnectionManagerImpl::ActiveStream::decodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;

  request_headers_ = std::move(headers);
  ENVOY_STREAM_LOG(debug, "request headers complete (end_stream={}):", *this, end_stream);
#ifndef NVLOG
  request_headers_->iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        ENVOY_STREAM_LOG(debug, "  '{}':'{}'", *static_cast<ActiveStream*>(context),
                         header.key().c_str(), header.value().c_str());
        return HeaderMap::Iterate::Continue;
      },
      this);
#endif

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
    // The protocol may have shifted in the HTTP/1.0 case so reset it.
    request_info_.protocol(protocol);
    HeaderMapImpl headers{
        {Headers::get().Status, std::to_string(enumToInt(Code::UpgradeRequired))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Require host header. For HTTP/1.1 Host has already been translated to :authority.
  if (!request_headers_->Host()) {
    HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::BadRequest))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Check for maximum incoming header size. Both codecs have some amount of checking for maximum
  // header size. For HTTP/1.1 the entire headers data has be less than ~80K (hard coded in
  // http_parser). For HTTP/2 the default allowed header block length is 64k.
  // In order to have generally uniform behavior we also check total header size here and keep it
  // under 60K. Ultimately it would be nice to have a configuration option ranging from the largest
  // header size http_parser and nghttp2 will allow, down to 16k or 8k for
  // envoy users who do not wish to proxy large headers.
  if (request_headers_->byteSize() > (60 * 1024)) {
    HeaderMapImpl headers{
        {Headers::get().Status, std::to_string(enumToInt(Code::RequestHeaderFieldsTooLarge))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Currently we only support relative paths at the application layer. We expect the codec to have
  // broken the path into pieces if applicable. NOTE: Currently the HTTP/1.1 codec does not do this
  // so we only support relative paths in all cases. https://tools.ietf.org/html/rfc7230#section-5.3
  // We also need to check for the existence of :path because CONNECT does not have a path, and we
  // don't support that currently.
  if (!request_headers_->Path() || request_headers_->Path()->value().c_str()[0] != '/') {
    connection_manager_.stats_.named_.downstream_rq_non_relative_path_.inc();
    HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::NotFound))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  if (protocol == Protocol::Http11 && request_headers_->Connection() &&
      0 ==
          StringUtil::caseInsensitiveCompare(request_headers_->Connection()->value().c_str(),
                                             Http::Headers::get().ConnectionValues.Close.c_str())) {
    state_.saw_connection_close_ = true;
  }

  // TODO(mattklein123): We should set downstream_local_address_ and downstream_remote_address_
  // as early as possible in this function since there are various places where we can return and
  // still log before we get here.
  request_info_.downstream_local_address_ =
      connection_manager_.read_callbacks_->connection().localAddress();
  request_info_.downstream_remote_address_ = ConnectionManagerUtility::mutateRequestHeaders(
      *request_headers_, protocol, connection_manager_.read_callbacks_->connection(),
      connection_manager_.config_, *snapped_route_config_, connection_manager_.random_generator_,
      connection_manager_.runtime_, connection_manager_.local_info_);
  ASSERT(request_info_.downstream_remote_address_ != nullptr);

  ASSERT(!cached_route_.valid());
  refreshCachedRoute();

  // Check for WebSocket upgrade request if the route exists, and supports WebSockets.
  // TODO if there are no filters when starting a filter iteration, the connection manager
  // should return 404. The current returns no response if there is no router filter.
  if (protocol == Protocol::Http11 && cached_route_.value()) {
    const Router::RouteEntry* route_entry = cached_route_.value()->routeEntry();
    const bool websocket_allowed = (route_entry != nullptr) && route_entry->useWebSocket();
    const bool websocket_requested = Utility::isWebSocketUpgradeRequest(*request_headers_);

    if (websocket_requested && websocket_allowed) {
      ENVOY_STREAM_LOG(debug, "found websocket connection. (end_stream={}):", *this, end_stream);

      connection_manager_.ws_connection_.reset(new WebSocket::WsHandlerImpl(
          *request_headers_, request_info_, *route_entry, *this,
          connection_manager_.cluster_manager_, connection_manager_.read_callbacks_));
      connection_manager_.ws_connection_->onNewConnection();
      connection_manager_.stats_.named_.downstream_cx_websocket_active_.inc();
      connection_manager_.stats_.named_.downstream_cx_http1_active_.dec();
      connection_manager_.stats_.named_.downstream_cx_websocket_total_.inc();
      return;
    } else if (websocket_requested) {
      // Do not allow WebSocket upgrades if the route does not support it.
      connection_manager_.stats_.named_.downstream_rq_ws_on_non_ws_route_.inc();
      HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::Forbidden))}};
      encodeHeaders(nullptr, headers, true);
      return;
    }
    // Allow non websocket requests to go through websocket enabled routes.
  }

  // Check if tracing is enabled at all.
  if (connection_manager_.config_.tracingConfig()) {
    traceRequest();
  }

  decodeHeaders(nullptr, *request_headers_, end_stream);
}

void ConnectionManagerImpl::ActiveStream::traceRequest() {
  Tracing::Decision tracing_decision =
      Tracing::HttpTracerUtility::isTracing(request_info_, *request_headers_);
  ConnectionManagerImpl::chargeTracingStats(tracing_decision.reason,
                                            connection_manager_.config_.tracingStats());

  if (!tracing_decision.is_tracing) {
    return;
  }

  active_span_ = connection_manager_.tracer_.startSpan(*this, *request_headers_, request_info_);

  // TODO: Need to investigate the following code based on the cached route, as may
  // be broken in the case a filter changes the route.

  // If a decorator has been defined, apply it to the active span.
  if (cached_route_.value() && cached_route_.value()->decorator()) {
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
        active_span_->setOperation(req_operation_override->value().c_str());

        // Clear the decorated operation so won't be used in the response header, as
        // it has been overridden by the inbound decorator operation request header.
        decorated_operation_ = nullptr;
      }
      // Remove header so not propagated to service
      request_headers_->removeEnvoyDecoratorOperation();
    }
  }

  // Inject the active span's tracing context into the request headers.
  active_span_->injectContext(*request_headers_);
}

void ConnectionManagerImpl::ActiveStream::decodeHeaders(ActiveStreamDecoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry;
  std::list<ActiveStreamDecoderFilterPtr>::iterator continue_data_entry = decoder_filters_.end();
  if (!filter) {
    entry = decoder_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeHeaders));
    state_.filter_call_state_ |= FilterCallState::DecodeHeaders;
    FilterHeadersStatus status = (*entry)->handle_->decodeHeaders(
        headers, end_stream && continue_data_entry == decoder_filters_.end());
    state_.filter_call_state_ &= ~FilterCallState::DecodeHeaders;
    ENVOY_STREAM_LOG(trace, "decode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterHeadersCallback(status) &&
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
    // decodeHeaders() again. Fake setting stopped_ since the continueDecoding() code expects it.
    ASSERT(buffered_request_data_);
    (*continue_data_entry)->stopped_ = true;
    (*continue_data_entry)->continueDecoding();
  }
}

void ConnectionManagerImpl::ActiveStream::decodeData(Buffer::Instance& data, bool end_stream) {
  request_info_.bytes_received_ += data.length();
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;
  if (state_.remote_complete_) {
    ENVOY_STREAM_LOG(debug, "request end stream", *this);
  }

  decodeData(nullptr, data, end_stream);
}

void ConnectionManagerImpl::ActiveStream::decodeData(ActiveStreamDecoderFilter* filter,
                                                     Buffer::Instance& data, bool end_stream) {
  // If a response is complete or a reset has been sent, filters do not care about further body
  // data. Just drop it.
  if (state_.local_complete_) {
    return;
  }

  std::list<ActiveStreamDecoderFilterPtr>::iterator entry;
  if (!filter) {
    entry = decoder_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeData));
    state_.filter_call_state_ |= FilterCallState::DecodeData;
    FilterDataStatus status = (*entry)->handle_->decodeData(data, end_stream);
    state_.filter_call_state_ &= ~FilterCallState::DecodeData;
    ENVOY_STREAM_LOG(trace, "decode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.decoder_filters_streaming_)) {
      return;
    }
  }
}

void ConnectionManagerImpl::ActiveStream::addDecodedData(ActiveStreamDecoderFilter& filter,
                                                         Buffer::Instance& data, bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::DecodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::DecodeData)) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.decoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::DecodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    decodeData(&filter, data, false);
  } else {
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED;
  }
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(HeaderMapPtr&& trailers) {
  request_trailers_ = std::move(trailers);
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = true;
  decodeTrailers(nullptr, *request_trailers_);
}

void ConnectionManagerImpl::ActiveStream::decodeTrailers(ActiveStreamDecoderFilter* filter,
                                                         HeaderMap& trailers) {
  // See decodeData() above for why we check local_complete_ here.
  if (state_.local_complete_) {
    return;
  }

  std::list<ActiveStreamDecoderFilterPtr>::iterator entry;
  if (!filter) {
    entry = decoder_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeTrailers));
    state_.filter_call_state_ |= FilterCallState::DecodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->decodeTrailers(trailers);
    state_.filter_call_state_ &= ~FilterCallState::DecodeTrailers;
    ENVOY_STREAM_LOG(trace, "decode trailers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }
}

std::list<ConnectionManagerImpl::ActiveStreamEncoderFilterPtr>::iterator
ConnectionManagerImpl::ActiveStream::commonEncodePrefix(ActiveStreamEncoderFilter* filter,
                                                        bool end_stream) {
  // Only do base state setting on the initial call. Subsequent calls for filtering do not touch
  // the base state.
  if (filter == nullptr) {
    ASSERT(!state_.local_complete_);
    state_.local_complete_ = end_stream;
  }

  if (!filter) {
    return encoder_filters_.begin();
  } else {
    return std::next(filter->entry());
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

void ConnectionManagerImpl::ActiveStream::refreshCachedRoute() {
  Router::RouteConstSharedPtr route = snapped_route_config_->route(*request_headers_, stream_id_);
  request_info_.route_entry_ = route ? route->routeEntry() : nullptr;
  cached_route_.value(std::move(route));
}

void ConnectionManagerImpl::ActiveStream::encode100ContinueHeaders(
    ActiveStreamEncoderFilter* filter, HeaderMap& headers) {
  ASSERT(connection_manager_.config_.proxy100Continue());
  // Make sure commonContinue continues encode100ContinueHeaders.
  has_continue_headers_ = true;

  // Similar to the block in encodeHeaders, run encode100ContinueHeaders on each
  // filter. This is simpler than that case because 100 continue implies no
  // end-stream, and because there are normal headers coming there's no need for
  // complex continuation logic.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, false);
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
  ConnectionManagerUtility::mutateResponseHeaders(headers, *request_headers_);

  // Count both the 1xx and follow-up response code in stats.
  chargeStats(headers);

  ENVOY_STREAM_LOG(debug, "encoding 100 continue headers via codec", *this);
#ifndef NVLOG
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        ENVOY_STREAM_LOG(debug, "  '{}':'{}'", *static_cast<ActiveStream*>(context),
                         header.key().c_str(), header.value().c_str());
        return HeaderMap::Iterate::Continue;
      },
      this);
#endif

  // Now actually encode via the codec.
  response_encoder_->encode100ContinueHeaders(headers);
}

void ConnectionManagerImpl::ActiveStream::encodeHeaders(ActiveStreamEncoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, end_stream);
  std::list<ActiveStreamEncoderFilterPtr>::iterator continue_data_entry = encoder_filters_.end();

  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeHeaders));
    state_.filter_call_state_ |= FilterCallState::EncodeHeaders;
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(
        headers, end_stream && continue_data_entry == encoder_filters_.end());
    state_.filter_call_state_ &= ~FilterCallState::EncodeHeaders;
    ENVOY_STREAM_LOG(trace, "encode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterHeadersCallback(status)) {
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
  headers.insertServer().value().setReference(connection_manager_.config_.serverName());
  ConnectionManagerUtility::mutateResponseHeaders(headers, *request_headers_);

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

  // If we are destroying a stream before remote is complete and the connection does not support
  // multiplexing, we should disconnect since we don't want to wait around for the request to
  // finish.
  if (!state_.remote_complete_) {
    if (connection_manager_.codec_->protocol() != Protocol::Http2) {
      connection_manager_.drain_state_ = DrainState::Closing;
    }

    connection_manager_.stats_.named_.downstream_rq_response_before_rq_complete_.inc();
  }

  if (connection_manager_.drain_state_ == DrainState::Closing &&
      connection_manager_.codec_->protocol() != Protocol::Http2) {
    headers.insertConnection().value().setReference(Headers::get().ConnectionValues.Close);
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
        if (!resp_operation_override->value().empty()) {
          active_span_->setOperation(resp_operation_override->value().c_str());
        }
        // Remove header so not propagated to service.
        headers.removeEnvoyDecoratorOperation();
      }
    }
  }

  chargeStats(headers);

  ENVOY_STREAM_LOG(debug, "encoding headers via codec (end_stream={}):", *this,
                   end_stream && continue_data_entry == encoder_filters_.end());
#ifndef NVLOG
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        ENVOY_STREAM_LOG(debug, "  '{}':'{}'", *static_cast<ActiveStream*>(context),
                         header.key().c_str(), header.value().c_str());
        return HeaderMap::Iterate::Continue;
      },
      this);
#endif

  // Now actually encode via the codec.
  response_encoder_->encodeHeaders(headers,
                                   end_stream && continue_data_entry == encoder_filters_.end());

  if (continue_data_entry != encoder_filters_.end()) {
    // We use the continueEncoding() code since it will correctly handle not calling
    // encodeHeaders() again. Fake setting stopped_ since the continueEncoding() code expects it.
    ASSERT(buffered_response_data_);
    (*continue_data_entry)->stopped_ = true;
    (*continue_data_entry)->continueEncoding();
  } else {
    maybeEndEncode(end_stream);
  }
}

void ConnectionManagerImpl::ActiveStream::addEncodedData(ActiveStreamEncoderFilter& filter,
                                                         Buffer::Instance& data, bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::EncodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::EncodeData)) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.encoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::EncodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    encodeData(&filter, data, false);
  } else {
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED;
  }
}

void ConnectionManagerImpl::ActiveStream::encodeData(ActiveStreamEncoderFilter* filter,
                                                     Buffer::Instance& data, bool end_stream) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, end_stream);
  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeData));
    state_.filter_call_state_ |= FilterCallState::EncodeData;
    FilterDataStatus status = (*entry)->handle_->encodeData(data, end_stream);
    state_.filter_call_state_ &= ~FilterCallState::EncodeData;
    ENVOY_STREAM_LOG(trace, "encode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.encoder_filters_streaming_)) {
      return;
    }
  }

  ENVOY_STREAM_LOG(trace, "encoding data via codec (size={} end_stream={})", *this, data.length(),
                   end_stream);

  request_info_.bytes_sent_ += data.length();
  response_encoder_->encodeData(data, end_stream);
  maybeEndEncode(end_stream);
}

void ConnectionManagerImpl::ActiveStream::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                                         HeaderMap& trailers) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, true);
  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeTrailers));
    state_.filter_call_state_ |= FilterCallState::EncodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->encodeTrailers(trailers);
    state_.filter_call_state_ &= ~FilterCallState::EncodeTrailers;
    ENVOY_STREAM_LOG(trace, "encode trailers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  ENVOY_STREAM_LOG(debug, "encoding trailers via codec", *this);
#ifndef NVLOG
  trailers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        ENVOY_STREAM_LOG(debug, "  '{}':'{}'", *static_cast<ActiveStream*>(context),
                         header.key().c_str(), header.value().c_str());
        return HeaderMap::Iterate::Continue;
      },
      this);
#endif

  response_encoder_->encodeTrailers(trailers);
  maybeEndEncode(true);
}

void ConnectionManagerImpl::ActiveStream::maybeEndEncode(bool end_stream) {
  if (end_stream) {
    request_timer_->complete();
    connection_manager_.doEndStream(*this);
  }
}

void ConnectionManagerImpl::ActiveStream::onResetStream(StreamResetReason) {
  // NOTE: This function gets called in all of the following cases:
  //       1) We TX an app level reset
  //       2) The codec TX a codec level reset
  //       3) The codec RX a reset
  //       If we need to differentiate we need to do it inside the codec. Can start with this.
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

void ConnectionManagerImpl::ActiveStream::callHighWatermarkCallbacks() {
  ++high_watermark_count_;
  if (watermark_callbacks_) {
    watermark_callbacks_->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionManagerImpl::ActiveStream::callLowWatermarkCallbacks() {
  ASSERT(high_watermark_count_ > 0);
  --high_watermark_count_;
  if (watermark_callbacks_) {
    watermark_callbacks_->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionManagerImpl::ActiveStream::setBufferLimit(uint32_t new_limit) {
  buffer_limit_ = new_limit;
  if (buffered_request_data_) {
    buffered_request_data_->setWatermarks(buffer_limit_);
  }
  if (buffered_response_data_) {
    buffered_response_data_->setWatermarks(buffer_limit_);
  }
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
  ASSERT(stopped_);
  stopped_ = false;

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

  // TODO(mattklein123): If a filter returns StopIterationNoBuffer and then does a continue, we
  // won't be able to end the stream if there is no buffered data. Need to handle this.
  if (bufferedData()) {
    doData(complete() && !trailers());
  }

  if (trailers()) {
    doTrailers();
  }
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfter100ContinueHeadersCallback(
    FilterHeadersStatus status) {
  ASSERT(parent_.has_continue_headers_);
  ASSERT(!continue_headers_continued_);
  ASSERT(!stopped_);

  if (status == FilterHeadersStatus::StopIteration) {
    stopped_ = true;
    return false;
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    continue_headers_continued_ = true;
    return true;
  }
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterHeadersCallback(
    FilterHeadersStatus status) {
  ASSERT(!headers_continued_);
  ASSERT(!stopped_);

  if (status == FilterHeadersStatus::StopIteration) {
    stopped_ = true;
    return false;
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    headers_continued_ = true;
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
    if (stopped_) {
      commonHandleBufferData(provided_data);
      commonContinue();
      return false;
    } else {
      ASSERT(headers_continued_);
    }
  } else {
    stopped_ = true;
    if (status == FilterDataStatus::StopIterationAndBuffer ||
        status == FilterDataStatus::StopIterationAndWatermark) {
      buffer_was_streaming = status == FilterDataStatus::StopIterationAndWatermark;
      commonHandleBufferData(provided_data);
    }

    return false;
  }

  return true;
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterTrailersCallback(
    FilterTrailersStatus status) {

  if (status == FilterTrailersStatus::Continue) {
    if (stopped_) {
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

RequestInfo::RequestInfo& ConnectionManagerImpl::ActiveStreamFilterBase::requestInfo() {
  return parent_.request_info_;
}

Tracing::Span& ConnectionManagerImpl::ActiveStreamFilterBase::activeSpan() {
  if (parent_.active_span_) {
    return *parent_.active_span_;
  } else {
    return Tracing::NullSpan::instance();
  }
}

Tracing::Config& ConnectionManagerImpl::ActiveStreamFilterBase::tracingConfig() { return parent_; }

Router::RouteConstSharedPtr ConnectionManagerImpl::ActiveStreamFilterBase::route() {
  if (!parent_.cached_route_.valid()) {
    parent_.refreshCachedRoute();
  }

  return parent_.cached_route_.value();
}

void ConnectionManagerImpl::ActiveStreamFilterBase::clearRouteCache() {
  parent_.cached_route_ = Optional<Router::RouteConstSharedPtr>();
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamDecoderFilter::createBuffer() {
  auto buffer = Buffer::WatermarkBufferPtr{
      new Buffer::WatermarkBuffer([this]() -> void { this->requestDataDrained(); },
                                  [this]() -> void { this->requestDataTooLarge(); })};
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::addDecodedData(Buffer::Instance& data,
                                                                      bool streaming) {
  parent_.addDecodedData(*this, data, streaming);
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
  parent_.encodeData(nullptr, data, end_stream);
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::encodeTrailers(HeaderMapPtr&& trailers) {
  parent_.response_trailers_ = std::move(trailers);
  parent_.encodeTrailers(nullptr, *parent_.response_trailers_);
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
    Http::Utility::sendLocalReply(*this, parent_.state_.destroyed_, Http::Code::PayloadTooLarge,
                                  CodeUtility::toString(Http::Code::PayloadTooLarge));
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
  // This is called exactly once per stream, by the router filter.
  // If there's ever a need for another filter to subscribe to watermark callbacks this can be
  // turned into a vector.
  ASSERT(parent_.watermark_callbacks_ == nullptr);
  parent_.watermark_callbacks_ = &watermark_callbacks;
  for (uint32_t i = 0; i < parent_.high_watermark_count_; ++i) {
    watermark_callbacks.onAboveWriteBufferHighWatermark();
  }
}
void ConnectionManagerImpl::ActiveStreamDecoderFilter::removeDownstreamWatermarkCallbacks(
    DownstreamWatermarkCallbacks& watermark_callbacks) {
  ASSERT(parent_.watermark_callbacks_ == &watermark_callbacks);
  UNREFERENCED_PARAMETER(watermark_callbacks);
  parent_.watermark_callbacks_ = nullptr;
}

Buffer::WatermarkBufferPtr ConnectionManagerImpl::ActiveStreamEncoderFilter::createBuffer() {
  auto buffer = new Buffer::WatermarkBuffer([this]() -> void { this->responseDataDrained(); },
                                            [this]() -> void { this->responseDataTooLarge(); });
  buffer->setWatermarks(parent_.buffer_limit_);
  return Buffer::WatermarkBufferPtr{buffer};
}

void ConnectionManagerImpl::ActiveStreamEncoderFilter::addEncodedData(Buffer::Instance& data,
                                                                      bool streaming) {
  return parent_.addEncodedData(*this, data, streaming);
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
      stopped_ = false;

      Http::Utility::sendLocalReply(
          [&](HeaderMapPtr&& response_headers, bool end_stream) -> void {
            parent_.response_headers_ = std::move(response_headers);
            parent_.response_encoder_->encodeHeaders(*parent_.response_headers_, end_stream);
          },
          [&](Buffer::Instance& data, bool end_stream) -> void {
            parent_.response_encoder_->encodeData(data, end_stream);
            parent_.state_.local_complete_ = end_stream;
            parent_.maybeEndEncode(end_stream);
          },
          parent_.state_.destroyed_, Http::Code::InternalServerError,
          CodeUtility::toString(Http::Code::InternalServerError));
    } else {
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
