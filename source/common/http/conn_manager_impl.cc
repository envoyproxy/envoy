#include "conn_manager_impl.h"
#include "conn_manager_utility.h"
#include "headers.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/drain_decision.h"
#include "envoy/stats/stats.h"
#include "envoy/tracing/http_tracer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

namespace Http {

ConnectionManagerStats ConnectionManagerImpl::generateStats(const std::string& prefix,
                                                            Stats::Store& stats) {
  return {
      {ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER_PREFIX(stats, prefix), POOL_GAUGE_PREFIX(stats, prefix),
                               POOL_TIMER_PREFIX(stats, prefix))},
      prefix,
      stats};
}

ConnectionManagerImpl::ConnectionManagerImpl(ConnectionManagerConfig& config,
                                             Network::DrainDecision& drain_close,
                                             Runtime::RandomGenerator& random_generator,
                                             Tracing::HttpTracer& tracer, Runtime::Loader& runtime)
    : config_(config), conn_length_(config_.stats().named_.downstream_cx_length_ms_.allocateSpan()),
      drain_close_(drain_close), random_generator_(random_generator), tracer_(tracer),
      runtime_(runtime) {}

void ConnectionManagerImpl::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  config_.stats().named_.downstream_cx_total_.inc();
  config_.stats().named_.downstream_cx_active_.inc();
  if (read_callbacks_->connection().ssl()) {
    config_.stats().named_.downstream_cx_ssl_total_.inc();
    config_.stats().named_.downstream_cx_ssl_active_.inc();
  }

  read_callbacks_->connection().addConnectionCallbacks(*this);

  if (config_.idleTimeout().valid()) {
    idle_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onIdleTimeout(); });
    idle_timer_->enableTimer(config_.idleTimeout().value());
  }
}

ConnectionManagerImpl::~ConnectionManagerImpl() {
  config_.stats().named_.downstream_cx_destroy_.inc();
  config_.stats().named_.downstream_cx_active_.dec();
  if (read_callbacks_->connection().ssl()) {
    config_.stats().named_.downstream_cx_ssl_active_.dec();
  }

  if (codec_) {
    if (codec_->protocolString() == Http1::PROTOCOL_STRING) {
      config_.stats().named_.downstream_cx_http1_active_.dec();
    } else {
      ASSERT(codec_->protocolString() == Http2::PROTOCOL_STRING);
      config_.stats().named_.downstream_cx_http2_active_.dec();
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

void ConnectionManagerImpl::destroyStream(ActiveStream& stream) {
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
    read_callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(streams_));
  }

  if (reset_stream && !(codec_->features() & CodecFeatures::Multiplexing)) {
    drain_state_ = DrainState::Closing;
  }

  checkForDeferredClose();

  // Reading may have been disabled for the non-multiplexing case, so enable it again.
  if (drain_state_ != DrainState::Closing && !(codec_->features() & CodecFeatures::Multiplexing) &&
      !read_callbacks_->connection().readEnabled()) {
    read_callbacks_->connection().readDisable(false);
  }

  if (idle_timer_ && streams_.empty()) {
    idle_timer_->enableTimer(config_.idleTimeout().value());
  }
}

StreamDecoder& ConnectionManagerImpl::newStream(StreamEncoder& response_encoder) {
  if (idle_timer_) {
    idle_timer_->disableTimer();
  }

  conn_log_debug("new stream", read_callbacks_->connection());
  ActiveStreamPtr new_stream(new ActiveStream(*this));
  new_stream->response_encoder_ = &response_encoder;
  new_stream->response_encoder_->getStream().addCallbacks(*new_stream);
  config_.filterFactory().createFilterChain(*new_stream);
  new_stream->moveIntoList(std::move(new_stream), streams_);
  return **streams_.begin();
}

Network::FilterStatus ConnectionManagerImpl::onData(Buffer::Instance& data) {
  if (!codec_) {
    codec_ = config_.createCodec(read_callbacks_->connection(), data, *this);
    if (codec_->protocolString() == Http1::PROTOCOL_STRING) {
      config_.stats().named_.downstream_cx_http1_total_.inc();
      config_.stats().named_.downstream_cx_http1_active_.inc();
    } else {
      ASSERT(codec_->protocolString() == Http2::PROTOCOL_STRING);
      config_.stats().named_.downstream_cx_http2_total_.inc();
      config_.stats().named_.downstream_cx_http2_active_.inc();
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
      conn_log_debug("dispatch error: {}", read_callbacks_->connection(), e.what());
      config_.stats().named_.downstream_cx_protocol_error_.inc();

      // In the protocol error case, we need to reset all streams now. Since we do a flush write,
      // the connection might stick around long enough for a pending stream to come back and try
      // to encode.
      resetAllStreams();

      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration;
    }

    // Processing incoming data may release outbound data so check for closure here as well.
    checkForDeferredClose();

    // The HTTP/1.1 codec will pause dispatch after a single message is complete. We want to
    // either redispatch if there are no streams and we have more data, or if we have a single
    // complete stream but have not responded yet we will pause socket reads to apply back pressure.
    if (!(codec_->features() & CodecFeatures::Multiplexing)) {
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

void ConnectionManagerImpl::onBufferChange(Network::ConnectionBufferType type, uint64_t,
                                           int64_t delta) {
  Network::Utility::updateBufferStats(type, delta,
                                      config_.stats().named_.downstream_cx_rx_bytes_total_,
                                      config_.stats().named_.downstream_cx_rx_bytes_buffered_,
                                      config_.stats().named_.downstream_cx_tx_bytes_total_,
                                      config_.stats().named_.downstream_cx_tx_bytes_buffered_);
}

void ConnectionManagerImpl::resetAllStreams() {
  while (!streams_.empty()) {
    // Mimic a downstream reset in this case.
    streams_.front()->onResetStream(StreamResetReason::ConnectionTermination);
  }
}

void ConnectionManagerImpl::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::LocalClose) {
    config_.stats().named_.downstream_cx_destroy_local_.inc();
  }

  if (events & Network::ConnectionEvent::RemoteClose) {
    config_.stats().named_.downstream_cx_destroy_remote_.inc();
  }

  if ((events & Network::ConnectionEvent::RemoteClose) ||
      (events & Network::ConnectionEvent::LocalClose)) {
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
    if (events & Network::ConnectionEvent::LocalClose) {
      config_.stats().named_.downstream_cx_destroy_local_active_rq_.inc();
    }
    if (events & Network::ConnectionEvent::RemoteClose) {
      config_.stats().named_.downstream_cx_destroy_remote_active_rq_.inc();
    }

    config_.stats().named_.downstream_cx_destroy_active_rq_.inc();
    user_agent_.onConnectionDestroy(events, true);
    resetAllStreams();
  }
}

void ConnectionManagerImpl::onGoAway() {
  // Currently we do nothing with remote go away frames. In the future we can decide to no longer
  // push resources if applicable.
}

void ConnectionManagerImpl::onIdleTimeout() {
  conn_log_debug("idle timeout", read_callbacks_->connection());
  config_.stats().named_.downstream_cx_idle_timeout_.inc();
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

DateFormatter ConnectionManagerImpl::ActiveStream::date_formatter_("%a, %d %b %Y %H:%M:%S GMT");

ConnectionManagerImpl::ActiveStream::ActiveStream(ConnectionManagerImpl& connection_manager)
    : connection_manager_(connection_manager),
      stream_id_(connection_manager.random_generator_.random()),
      request_timer_(connection_manager_.config_.stats().named_.downstream_rq_time_.allocateSpan()),
      request_info_(connection_manager_.codec_->protocolString()) {
  connection_manager_.config_.stats().named_.downstream_rq_total_.inc();
  connection_manager_.config_.stats().named_.downstream_rq_active_.inc();
  if (connection_manager_.codec_->protocolString() == Http1::PROTOCOL_STRING) {
    connection_manager_.config_.stats().named_.downstream_rq_http1_total_.inc();
  } else {
    ASSERT(connection_manager_.codec_->protocolString() == Http2::PROTOCOL_STRING);
    connection_manager_.config_.stats().named_.downstream_rq_http2_total_.inc();
  }
}

ConnectionManagerImpl::ActiveStream::~ActiveStream() {
  connection_manager_.config_.stats().named_.downstream_rq_active_.dec();
  for (AccessLog::InstancePtr access_log : connection_manager_.config_.accessLogs()) {
    access_log->log(request_headers_.get(), response_headers_.get(), request_info_);
  }

  if (connection_manager_.config_.isTracing()) {
    connection_manager_.tracer_.trace(request_headers_.get(), response_headers_.get(),
                                      request_info_);
  }
}

void ConnectionManagerImpl::ActiveStream::addStreamDecoderFilter(StreamDecoderFilterPtr filter) {
  ActiveStreamDecoderFilterPtr wrapper(new ActiveStreamDecoderFilter(*this, filter));
  filter->setDecoderFilterCallbacks(*wrapper);
  wrapper->moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void ConnectionManagerImpl::ActiveStream::addStreamEncoderFilter(StreamEncoderFilterPtr filter) {
  ActiveStreamEncoderFilterPtr wrapper(new ActiveStreamEncoderFilter(*this, filter));
  filter->setEncoderFilterCallbacks(*wrapper);
  wrapper->moveIntoListBack(std::move(wrapper), encoder_filters_);
}

void ConnectionManagerImpl::ActiveStream::addStreamFilter(StreamFilterPtr filter) {
  addStreamDecoderFilter(filter);
  addStreamEncoderFilter(filter);
}

void ConnectionManagerImpl::ActiveStream::chargeStats(HeaderMap& headers) {
  uint64_t response_code = Utility::getResponseStatus(headers);
  request_info_.response_code_.value(response_code);

  if (request_info_.hc_request_) {
    return;
  }

  if (CodeUtility::is2xx(response_code)) {
    connection_manager_.config_.stats().named_.downstream_rq_2xx_.inc();
  } else if (CodeUtility::is3xx(response_code)) {
    connection_manager_.config_.stats().named_.downstream_rq_3xx_.inc();
  } else if (CodeUtility::is4xx(response_code)) {
    connection_manager_.config_.stats().named_.downstream_rq_4xx_.inc();
  } else if (CodeUtility::is5xx(response_code)) {
    connection_manager_.config_.stats().named_.downstream_rq_5xx_.inc();
  }
}

uint64_t ConnectionManagerImpl::ActiveStream::connectionId() {
  return connection_manager_.read_callbacks_->connection().id();
}

void ConnectionManagerImpl::ActiveStream::decodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;

  request_headers_ = std::move(headers);
  stream_log_debug("request headers complete (end_stream={}):", *this, end_stream);
#ifndef NDEBUG
  request_headers_->iterate([this](const LowerCaseString& key, const std::string& value) -> void {
    stream_log_debug("  '{}':'{}'", *this, key.get(), value);
  });
#endif

  connection_manager_.user_agent_.initializeFromHeaders(*request_headers_,
                                                        connection_manager_.config_.stats().prefix_,
                                                        connection_manager_.config_.stats().store_);

  // Make sure we are getting a codec version we support.
  const std::string& codec_version = request_headers_->get(Headers::get().Version);
  if (!(codec_version == "HTTP/1.1" || codec_version == "HTTP/2")) {
    HeaderMapImpl headers{
        {Headers::get().Status, std::to_string(enumToInt(Code::UpgradeRequired))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Require host header. For HTTP/1.1 Host has already been translated to :host.
  if (request_headers_->get(Headers::get().Host).empty()) {
    HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::BadRequest))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Check for maximum incoming header size. Both codecs have some amount of checking for maximum
  // header size. For HTTP/1.1 the entire headers data has be less than ~80K (hard coded in
  // http_parser). For HTTP/2 we currently check if the incoming headers is less than ~63K. 63K
  // is abritrary and is set because currently nghttp2 does not allow *sending* more than 64K of
  // headers for reasons I don't understand so 63K is testable. However, since HTTP/1.1 can send us
  // potentially 80K of headers, we can still die when we try to proxy to HTTP/2. We correctly
  // handle this but to the rest of the code it looks like an upstream reset which will usually
  // result in a 503. In order to have generally uniform behavior we also check total header size
  // here and keep it under 60K. Ultimately it would be nice to bring this to a lower value but
  // unclear if that is possible or not.
  if (request_headers_->byteSize() > (60 * 1024)) {
    HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::BadRequest))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  // Currently we only support relative paths at the application layer. We expect the codec to have
  // broken the path into pieces if applicable. NOTE: Currently the HTTP/1.1 codec does not do this
  // so we only support relative paths in all cases.
  // https://tools.ietf.org/html/rfc7230#section-5.3
  if (request_headers_->get(Headers::get().Path).find('/') != 0) {
    connection_manager_.config_.stats().named_.downstream_rq_non_relative_path_.inc();
    HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(Code::NotFound))}};
    encodeHeaders(nullptr, headers, true);
    return;
  }

  ConnectionManagerUtility::mutateRequestHeaders(
      *request_headers_, connection_manager_.read_callbacks_->connection(),
      connection_manager_.config_, connection_manager_.random_generator_,
      connection_manager_.runtime_);

  // Set the trusted address for the connection by taking the last address in XFF.
  downstream_address_ = Utility::getLastAddressFromXFF(*request_headers_);
  decodeHeaders(nullptr, *request_headers_, end_stream);
}

void ConnectionManagerImpl::ActiveStream::decodeHeaders(ActiveStreamDecoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry;
  if (!filter) {
    entry = decoder_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != decoder_filters_.end(); entry++) {
    FilterHeadersStatus status = (*entry)->handle_->decodeHeaders(headers, end_stream);
    stream_log_trace("decode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterHeadersCallback(status)) {
      return;
    }
  }
}

void ConnectionManagerImpl::ActiveStream::decodeData(const Buffer::Instance& data,
                                                     bool end_stream) {
  request_info_.bytes_received_ += data.length();
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;
  if (state_.remote_complete_) {
    stream_log_debug("request end stream", *this);
  }

  // We are fed data directly from codec buffers. Perform a single copy here so that filters can
  // modify the data and potentially take ownership of it.
  Buffer::OwnedImpl data_copy(data);
  decodeData(nullptr, data_copy, end_stream);
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
    FilterDataStatus status = (*entry)->handle_->decodeData(data, end_stream);
    stream_log_trace("decode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterDataCallback(status, data)) {
      return;
    }
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
    FilterTrailersStatus status = (*entry)->handle_->decodeTrailers(trailers);
    stream_log_trace("decode trailers called: filter={} status={}", *this,
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

void ConnectionManagerImpl::ActiveStream::encodeHeaders(ActiveStreamEncoderFilter* filter,
                                                        HeaderMap& headers, bool end_stream) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, end_stream);
  for (; entry != encoder_filters_.end(); entry++) {
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(headers, end_stream);
    stream_log_trace("encode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterHeadersCallback(status)) {
      return;
    }
  }

  // Base headers.
  headers.replaceViaCopy(Headers::get().Server, connection_manager_.config_.serverName());
  headers.replaceViaMoveValue(Headers::get().Date, date_formatter_.now());
  headers.replaceViaCopy(Headers::get().EnvoyProtocolVersion,
                         connection_manager_.codec_->protocolString());
  ConnectionManagerUtility::mutateResponseHeaders(headers, *request_headers_,
                                                  connection_manager_.config_);

  // See if we want to drain/close the connection. Send the go away frame prior to encoding the
  // header block.
  if (connection_manager_.drain_state_ == DrainState::NotDraining &&
      connection_manager_.drain_close_.drainClose()) {

    // This doesn't really do anything for HTTP/1.1 other then give the connection another boost
    // of time to race with incoming requests. It mainly just keeps the logic the same between
    // HTTP/1.1 and HTTP/2.
    connection_manager_.startDrainSequence();
    connection_manager_.config_.stats().named_.downstream_cx_drain_close_.inc();
    stream_log_debug("drain closing connection", *this);
  }

  // If we are destroying a stream before remote is complete and the connection does not support
  // multiplexing, we should disconnect since we don't want to wait around for the request to
  // finish.
  if (!state_.remote_complete_) {
    if (!(connection_manager_.codec_->features() & CodecFeatures::Multiplexing)) {
      connection_manager_.drain_state_ = DrainState::Closing;
    }

    connection_manager_.config_.stats().named_.downstream_rq_response_before_rq_complete_.inc();
  }

  if (connection_manager_.drain_state_ == DrainState::Closing &&
      !(connection_manager_.codec_->features() & CodecFeatures::Multiplexing)) {
    headers.addViaCopy(Headers::get().Connection, Headers::get().ConnectionValues.Close);
  }

  chargeStats(headers);

  stream_log_debug("encoding headers via codec (end_stream={}):", *this, end_stream);
#ifndef NDEBUG
  headers.iterate([this](const LowerCaseString& key, const std::string& value)
                      -> void { stream_log_debug("  '{}':'{}'", *this, key.get(), value); });
#endif

  // Now actually encode via the codec.
  response_encoder_->encodeHeaders(headers, end_stream);
  maybeEndEncode(end_stream);
}

void ConnectionManagerImpl::ActiveStream::encodeData(ActiveStreamEncoderFilter* filter,
                                                     Buffer::Instance& data, bool end_stream) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, end_stream);
  for (; entry != encoder_filters_.end(); entry++) {
    FilterDataStatus status = (*entry)->handle_->encodeData(data, end_stream);
    stream_log_trace("encode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterDataCallback(status, data)) {
      return;
    }
  }

  stream_log_trace("encoding data via codec (size={} end_stream={})", *this, data.length(),
                   end_stream);

  request_info_.bytes_sent_ += data.length();
  response_encoder_->encodeData(data, end_stream);
  maybeEndEncode(end_stream);
}

void ConnectionManagerImpl::ActiveStream::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                                         HeaderMap& trailers) {
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry = commonEncodePrefix(filter, true);
  for (; entry != encoder_filters_.end(); entry++) {
    FilterTrailersStatus status = (*entry)->handle_->encodeTrailers(trailers);
    stream_log_trace("encode trailers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  stream_log_debug("encoding trailers via codec", *this);
#ifndef NDEBUG
  trailers.iterate([this](const LowerCaseString& key, const std::string& value)
                       -> void { stream_log_debug("  '{}':'{}'", *this, key.get(), value); });
#endif

  response_encoder_->encodeTrailers(trailers);
  maybeEndEncode(true);
}

void ConnectionManagerImpl::ActiveStream::maybeEndEncode(bool end_stream) {
  if (end_stream) {
    request_timer_->complete();
    connection_manager_.destroyStream(*this);
  }
}

void ConnectionManagerImpl::ActiveStream::onResetStream(StreamResetReason) {
  // NOTE: This function gets called in all of the following cases:
  //       1) We TX an app level reset
  //       2) The codec TX a codec level reset
  //       3) The codec RX a reset
  //       If we need to differentiate we need to do it inside the codec. Can start with this.
  connection_manager_.config_.stats().named_.downstream_rq_rx_reset_.inc();

  for (auto callback : reset_callbacks_) {
    callback();
  }

  connection_manager_.read_callbacks_->connection().dispatcher().deferredDelete(
      removeFromList(connection_manager_.streams_));
}

void ConnectionManagerImpl::ActiveStreamFilterBase::addResetStreamCallback(
    std::function<void()> callback) {
  parent_.reset_callbacks_.push_back(callback);
}

void ConnectionManagerImpl::ActiveStreamFilterBase::commonContinue() {
  stream_log_trace("continuing filter chain: filter={}", parent_, static_cast<const void*>(this));
  ASSERT(stopped_);
  stopped_ = false;

  // Make sure that we handle the zero byte data frame case. We make no effort to optimize this
  // case in terms of merging it into a header only request/response. This could be done in the
  // future.
  if (!headers_continued_) {
    headers_continued_ = true;
    doHeaders(complete() && !bufferedData() && !trailers());
  }

  // TODO: If a filter returns StopIterationNoBuffer and then does a continue, we won't be able to
  //       end the stream if there is no buffered data. Need to handle this.
  if (bufferedData()) {
    doData(complete() && !trailers());
  }

  if (trailers()) {
    doTrailers();
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
      bufferedData().reset(new Buffer::OwnedImpl());
    }
    bufferedData()->add(provided_data);
  }
}

bool ConnectionManagerImpl::ActiveStreamFilterBase::commonHandleAfterDataCallback(
    FilterDataStatus status, Buffer::Instance& provided_data) {

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
    if (status == FilterDataStatus::StopIterationAndBuffer) {
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

uint64_t ConnectionManagerImpl::ActiveStreamFilterBase::connectionId() {
  return parent_.connectionId();
}

Event::Dispatcher& ConnectionManagerImpl::ActiveStreamFilterBase::dispatcher() {
  return parent_.connection_manager_.read_callbacks_->connection().dispatcher();
}

AccessLog::RequestInfo& ConnectionManagerImpl::ActiveStreamFilterBase::requestInfo() {
  return parent_.request_info_;
}

void ConnectionManagerImpl::ActiveStreamDecoderFilter::continueDecoding() { commonContinue(); }

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

void ConnectionManagerImpl::ActiveStreamEncoderFilter::continueEncoding() { commonContinue(); }

void ConnectionManagerImpl::ActiveStreamFilterBase::resetStream() {
  parent_.connection_manager_.config_.stats().named_.downstream_rq_tx_reset_.inc();
  parent_.connection_manager_.destroyStream(this->parent_);
}

uint64_t ConnectionManagerImpl::ActiveStreamFilterBase::streamId() { return parent_.stream_id_; }

const std::string& ConnectionManagerImpl::ActiveStreamFilterBase::downstreamAddress() {
  return parent_.downstream_address_;
}

} // Http
