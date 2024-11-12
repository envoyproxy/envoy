#include "library/common/http/client.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "library/common/bridge/utility.h"
#include "library/common/stream_info/extra_stream_info.h"
#include "library/common/system/system_helper.h"

namespace Envoy {
namespace Http {

/**
 * IMPORTANT: stream closure semantics in envoy mobile depends on the fact that the HCM fires a
 * stream reset when the remote side of the stream is closed but the local side remains open.
 * In other words the HCM (like the rest of Envoy) disallows locally half-open streams.
 * If this changes in Envoy, this file will need to change as well.
 * For implementation details @see Client::DirectStreamCallbacks::closeRemote.
 */

namespace {

constexpr auto SlowCallbackWarningThreshold = std::chrono::seconds(1);

} // namespace

Client::DirectStreamCallbacks::DirectStreamCallbacks(DirectStream& direct_stream,
                                                     EnvoyStreamCallbacks&& stream_callbacks,
                                                     Client& http_client)
    : direct_stream_(direct_stream), stream_callbacks_(std::move(stream_callbacks)),
      http_client_(http_client), explicit_flow_control_(direct_stream_.explicit_flow_control_) {}

Client::DirectStreamCallbacks::~DirectStreamCallbacks() {}

void Client::DirectStreamCallbacks::encodeHeaders(const ResponseHeaderMap& headers,
                                                  bool end_stream) {
  ScopeTrackerScopeState scope(&direct_stream_, http_client_.scopeTracker());
  ENVOY_LOG(debug, "[S{}] response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);

  ASSERT(
      http_client_.getStream(direct_stream_.stream_handle_, GetStreamFilters::AllowForAllStreams));

  // Capture some metadata before potentially closing the stream.
  absl::string_view alpn = "";
  OptRef<RequestDecoder> request_decoder = direct_stream_.requestDecoder();
  if (request_decoder) {
    direct_stream_.saveLatestStreamIntel();
    const auto& info = request_decoder->streamInfo();
    // Set the initial number of bytes consumed for the non terminal callbacks.
    direct_stream_.stream_intel_.consumed_bytes_from_response =
        info.getUpstreamBytesMeter() ? info.getUpstreamBytesMeter()->headerBytesReceived() : 0;
    // Capture the alpn if available.
    if (info.upstreamInfo() && info.upstreamInfo()->upstreamSslConnection()) {
      alpn = info.upstreamInfo()->upstreamSslConnection()->alpn();
    }
  }

  if (end_stream) {
    closeStream();
  }

  // Track success for later bookkeeping (stream could still be reset).
  uint64_t response_status = Utility::getResponseStatus(headers);
  success_ = CodeUtility::is2xx(response_status);

  ENVOY_LOG(debug, "[S{}] dispatching to platform response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_headers_callback_latency_, http_client_.timeSource());

  if (alpn.empty()) {
    stream_callbacks_.on_headers_(headers, end_stream, streamIntel());
  } else {
    auto new_headers = ResponseHeaderMapImpl::create();
    ResponseHeaderMapImpl::copyFrom(*new_headers, headers);
    new_headers->addCopy(LowerCaseString("x-envoy-upstream-alpn"), alpn);
    stream_callbacks_.on_headers_(*new_headers, end_stream, streamIntel());
  }

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_headers_cb", "{}ms", elapsed.count());
  }

  response_headers_forwarded_ = true;
  if (end_stream) {
    onComplete();
  }
}

uint32_t calculateBytesToSend(const Buffer::Instance& data, uint32_t max_bytes) {
  if (max_bytes == 0) {
    return data.length();
  }
  return std::min<uint32_t>(max_bytes, data.length());
}

void Client::DirectStreamCallbacks::encodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&direct_stream_, http_client_.scopeTracker());
  ENVOY_LOG(debug, "[S{}] response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);

  ASSERT(
      http_client_.getStream(direct_stream_.stream_handle_, GetStreamFilters::AllowForAllStreams));
  direct_stream_.saveLatestStreamIntel();
  if (end_stream) {
    closeStream();
  }

  // The response_data_ is systematically assigned here because resumeData can
  // incur an asynchronous callback to sendDataToBridge.
  if (explicit_flow_control_ && !response_data_) {
    response_data_ = std::make_unique<Buffer::WatermarkBuffer>(
        [this]() -> void { onBufferedDataDrained(); }, [this]() -> void { onHasBufferedData(); },
        []() -> void {});
    // Default to 2M per stream. This is fairly arbitrary and will result in
    // Envoy buffering up to 1M + flow-control-window for HTTP/2 and HTTP/3,
    // and having local data of 2M + kernel-buffer-limit for HTTP/1.1
    response_data_->setWatermarks(2 * 1024 * 1024);
  }

  // Send data if in default flow control mode, or if resumeData has been called in explicit
  // flow control mode.
  if (bytes_to_send_ > 0 || !explicit_flow_control_) {
    // We shouldn't be calling sendDataToBridge with newly arrived data if there's buffered data.
    ASSERT(!response_data_.get() || response_data_->length() == 0);
    sendData(data, end_stream);
  }

  // If not all the bytes have been sent up, buffer any remaining data in response_data.
  if (data.length() != 0) {
    ASSERT(explicit_flow_control_);
    ENVOY_LOG(
        debug, "[S{}] buffering {} bytes due to explicit flow control. {} total bytes buffered.",
        direct_stream_.stream_handle_, data.length(), data.length() + response_data_->length());
    response_data_->move(data);
  }
}

void Client::DirectStreamCallbacks::sendData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!explicit_flow_control_ || bytes_to_send_ > 0);

  // Cap by bytes_to_send_ if and only if applying explicit flow control.
  uint32_t bytes_to_send = calculateBytesToSend(data, bytes_to_send_);
  // Update the number of bytes consumed by this non terminal callback.
  direct_stream_.stream_intel_.consumed_bytes_from_response += bytes_to_send;
  // Only send end stream if all data is being sent.
  bool send_end_stream = end_stream && (bytes_to_send == data.length());

  ENVOY_LOG(debug,
            "[S{}] dispatching to platform response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, bytes_to_send, send_end_stream);

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_data_callback_latency_, http_client_.timeSource());

  // Make sure that when using explicit flow control this won't send more data until the next call
  // to resumeData. Set before on-data to handle reentrant callbacks.
  bytes_to_send_ = 0;

  stream_callbacks_.on_data_(data, bytes_to_send, send_end_stream, streamIntel());
  // We can drain the data up to bytes_to_send since we are done with it.
  data.drain(bytes_to_send);

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_data_cb", "{}ms", elapsed.count());
  }

  if (send_end_stream) {
    onComplete();
  } else if (explicit_flow_control_ && data.length() == 0 && error_.has_value()) {
    onError();
  }
}

void Client::DirectStreamCallbacks::encodeTrailers(const ResponseTrailerMap& trailers) {
  ScopeTrackerScopeState scope(&direct_stream_, http_client_.scopeTracker());
  ENVOY_LOG(debug, "[S{}] response trailers for stream:\n{}", direct_stream_.stream_handle_,
            trailers);

  ASSERT(
      http_client_.getStream(direct_stream_.stream_handle_, GetStreamFilters::AllowForAllStreams));
  direct_stream_.saveLatestStreamIntel();
  closeStream(); // Trailers always indicate the end of the stream.

  // For explicit flow control, don't send data unless prompted.
  if (explicit_flow_control_ && bytes_to_send_ == 0) {
    response_trailers_ = ResponseTrailerMapImpl::create();
    HeaderMapImpl::copyFrom(*response_trailers_, trailers);
    return;
  }

  sendTrailers(trailers);
}

void Client::DirectStreamCallbacks::sendTrailers(const ResponseTrailerMap& trailers) {
  ENVOY_LOG(debug, "[S{}] dispatching to platform response trailers for stream:\n{}",
            direct_stream_.stream_handle_, trailers);

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_trailers_callback_latency_, http_client_.timeSource());

  stream_callbacks_.on_trailers_(trailers, streamIntel());

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_trailers_cb", "{}ms", elapsed.count());
  }

  onComplete();
}

void Client::DirectStreamCallbacks::resumeData(size_t bytes_to_send) {
  ASSERT(explicit_flow_control_);
  ASSERT(bytes_to_send > 0);

  bytes_to_send_ = bytes_to_send;

  ENVOY_LOG(debug, "[S{}] received resume data call for {} bytes", direct_stream_.stream_handle_,
            bytes_to_send_);

  // If there is buffered data, send up to bytes_to_send bytes.
  // Make sure to send end stream with data only if
  // 1) it has been received from the peer and
  // 2) there are no trailers
  if (hasDataToSend()) {
    sendData(*response_data_, remote_end_stream_received_ && !response_trailers_.get());
    bytes_to_send_ = 0;
  }

  // If all buffered data has been sent, send and free up trailers.
  if (!hasDataToSend() && response_trailers_.get() && bytes_to_send_ > 0) {
    sendTrailers(*response_trailers_);
    response_trailers_.reset();
    bytes_to_send_ = 0;
  }
}

void Client::DirectStreamCallbacks::closeStream(bool end_stream) {
  ENVOY_LOG(debug, "[S{}] close stream end stream {}\n", direct_stream_.stream_handle_, end_stream);
  remote_end_stream_received_ |= end_stream;
  if (end_stream) {
    // Latch stream intel on stream completion, as the stream info will go away.
    // If end_stream is false this is the stream reset case and data is latched
    // in resetStream
    direct_stream_.saveFinalStreamIntel();
  }

  auto& client = direct_stream_.parent_;
  auto stream =
      client.getStream(direct_stream_.stream_handle_, GetStreamFilters::AllowOnlyForOpenStreams);
  ASSERT(stream != nullptr);
  if (stream) {
    ENVOY_LOG(debug, "[S{}] erased stream\n", direct_stream_.stream_handle_);
    client.closed_streams_.emplace(direct_stream_.stream_handle_, std::move(stream));
    size_t erased = client.streams_.erase(direct_stream_.stream_handle_);
    ASSERT(erased == 1, "closeStream should always remove one entry from the streams map");
  }
  direct_stream_.request_decoder_ = nullptr;
}

void Client::DirectStreamCallbacks::onComplete() {
  direct_stream_.notifyAdapter(DirectStream::AdapterSignal::EncodeComplete);
  http_client_.removeStream(direct_stream_.stream_handle_);
  remote_end_stream_forwarded_ = true;
  ENVOY_LOG(debug, "[S{}] complete stream (success={})", direct_stream_.stream_handle_, success_);
  if (success_) {
    http_client_.stats().stream_success_.inc();
  } else {
    http_client_.stats().stream_failure_.inc();
  }

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_complete_callback_latency_, http_client_.timeSource());

  stream_callbacks_.on_complete_(streamIntel(), finalStreamIntel());

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_complete_cb", "{}ms", elapsed.count());
  }
}

void Client::DirectStreamCallbacks::onError() {
  direct_stream_.notifyAdapter(DirectStream::AdapterSignal::Error);
  ScopeTrackerScopeState scope(&direct_stream_, http_client_.scopeTracker());
  ENVOY_LOG(debug, "[S{}] remote reset stream", direct_stream_.stream_handle_);

  // When using explicit flow control, if any response data has been sent (e.g. headers), response
  // errors must be deferred until after resumeData has been called.
  // TODO(goaway): What is the expected behavior when an error is received, held, and then another
  // error occurs (e.g., timeout)?

  if (explicit_flow_control_ && (hasDataToSend() || response_trailers_.get())) {
    ENVOY_LOG(debug, "[S{}] defering remote reset stream due to explicit flow control",
              direct_stream_.stream_handle_);
    if (direct_stream_.parent_.getStream(direct_stream_.stream_handle_,
                                         GetStreamFilters::AllowOnlyForOpenStreams)) {
      closeStream(false);
    }
    return;
  }

  http_client_.removeStream(direct_stream_.stream_handle_);
  direct_stream_.request_decoder_ = nullptr;
  sendError();
}

void Client::DirectStreamCallbacks::sendError() {
  if (remote_end_stream_forwarded_) {
    // If the request was not fully sent, but the response was complete, Envoy
    // will reset the stream after sending the fin bit. Don't pass this class of
    // errors up to the user.
    ENVOY_LOG(debug, "[S{}] not sending error as onComplete was called");
    return;
  }

  // The stream should no longer be preset in the map, because onError() was either called from a
  // terminal callback that mapped to an error or it was called in response to a resetStream().
  ASSERT(
      !http_client_.getStream(direct_stream_.stream_handle_, GetStreamFilters::AllowForAllStreams));

  ENVOY_LOG(debug, "[S{}] dispatching to platform remote reset stream",
            direct_stream_.stream_handle_);
  http_client_.stats().stream_failure_.inc();

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_error_callback_latency_, http_client_.timeSource());

  stream_callbacks_.on_error_(error_.value(), streamIntel(), finalStreamIntel());
  error_.reset();

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_error_cb", "{}ms", elapsed.count());
  }
}

void Client::DirectStreamCallbacks::onSendWindowAvailable() {
  ENVOY_LOG(debug, "[S{}] remote send window available", direct_stream_.stream_handle_);
  stream_callbacks_.on_send_window_available_(streamIntel());
}

void Client::DirectStreamCallbacks::onCancel() {
  ScopeTrackerScopeState scope(&direct_stream_, http_client_.scopeTracker());

  ENVOY_LOG(debug, "[S{}] dispatching to platform cancel stream", direct_stream_.stream_handle_);
  http_client_.stats().stream_cancel_.inc();

  // Attempt to latch the latest stream info. This will be a no-op if the stream
  // is already complete.
  direct_stream_.saveFinalStreamIntel();

  auto callback_time_ms = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      http_client_.stats().on_cancel_callback_latency_, http_client_.timeSource());

  stream_callbacks_.on_cancel_(streamIntel(), finalStreamIntel());

  callback_time_ms->complete();
  auto elapsed = callback_time_ms->elapsed();
  if (elapsed > SlowCallbackWarningThreshold) {
    ENVOY_LOG_EVENT(warn, "slow_on_cancel_cb", "{}ms", elapsed.count());
  }
}

void Client::DirectStreamCallbacks::onHasBufferedData() {
  // This call is potentially asynchronous, and may occur for a closed stream.
  if (!remote_end_stream_received_) {
    direct_stream_.runHighWatermarkCallbacks();
  }
}

void Client::DirectStreamCallbacks::onBufferedDataDrained() {
  // This call is potentially asynchronous, and may occur for a closed stream.
  if (!remote_end_stream_received_) {
    direct_stream_.runLowWatermarkCallbacks();
  }
}

envoy_stream_intel Client::DirectStreamCallbacks::streamIntel() {
  return direct_stream_.stream_intel_;
}

envoy_final_stream_intel& Client::DirectStreamCallbacks::finalStreamIntel() {
  return direct_stream_.envoy_final_stream_intel_;
}

void Client::DirectStream::saveLatestStreamIntel() {
  OptRef<RequestDecoder> request_decoder = requestDecoder();
  if (!request_decoder) {
    return;
  }
  const auto& info = request_decoder->streamInfo();
  if (info.upstreamInfo()) {
    stream_intel_.connection_id = info.upstreamInfo()->upstreamConnectionId().value_or(-1);
  }
  stream_intel_.stream_id = static_cast<uint64_t>(stream_handle_);
  stream_intel_.attempt_count = info.attemptCount().value_or(0);
}

void Client::DirectStream::saveFinalStreamIntel() {
  if (!parent_.getStream(stream_handle_, GetStreamFilters::AllowOnlyForOpenStreams)) {
    return;
  }
  OptRef<RequestDecoder> request_decoder = requestDecoder();
  if (!request_decoder) {
    return;
  }
  StreamInfo::setFinalStreamIntel(request_decoder->streamInfo(), parent_.dispatcher_.timeSource(),
                                  envoy_final_stream_intel_);
}

void Client::DirectStreamCallbacks::latchError() {
  if (error_.has_value()) {
    return; // Only latch error once.
  }
  error_ = EnvoyError{};

  OptRef<RequestDecoder> request_decoder = direct_stream_.requestDecoder();
  if (!request_decoder) {
    error_->message_ = "";
    return;
  }
  const auto& info = request_decoder->streamInfo();

  std::vector<std::string> error_msg_details;
  if (info.responseCode().has_value()) {
    error_->error_code_ = Bridge::Utility::errorCodeFromLocalStatus(
        static_cast<Http::Code>(info.responseCode().value()));
    error_msg_details.push_back(absl::StrCat("rc: ", info.responseCode().value()));
  } else if (StreamInfo::isStreamIdleTimeout(info)) {
    error_->error_code_ = ENVOY_REQUEST_TIMEOUT;
  } else {
    error_->error_code_ = ENVOY_STREAM_RESET;
  }

  error_msg_details.push_back(absl::StrCat("ec: ", error_->error_code_));
  std::vector<std::string> response_flags(info.responseFlags().size());
  std::transform(info.responseFlags().begin(), info.responseFlags().end(), response_flags.begin(),
                 [](StreamInfo::ResponseFlag flag) { return std::to_string(flag.value()); });
  error_msg_details.push_back(absl::StrCat("rsp_flags: ", absl::StrJoin(response_flags, ",")));
  if (info.protocol().has_value()) {
    // https://github.com/envoyproxy/envoy/blob/fbce85914421145b5ae3210c9313eced63e535b0/envoy/http/protocol.h#L13
    error_msg_details.push_back(absl::StrCat("http: ", *info.protocol()));
  }
  if (std::string resp_code_details = info.responseCodeDetails().value_or("");
      !resp_code_details.empty()) {
    error_msg_details.push_back(absl::StrCat("det: ", std::move(resp_code_details)));
  }
  // The format of the error message propogated to callbacks is:
  // rc: {value}|ec: {value}|rsp_flags: {value}|http: {value}|det: {value}
  //
  // Where envoy_rc is the HTTP response code from StreamInfo::responseCode().
  // envoy_ec is of the envoy_error_code_t enum type, and gets mapped from envoy_rc.
  // rsp_flags comes from StreamInfo::responseFlags().
  // det is the contents of StreamInfo::responseCodeDetails().
  error_->message_ = absl::StrJoin(std::move(error_msg_details), "|");
  error_->attempt_count_ = info.attemptCount().value_or(0);
}

Client::DirectStream::DirectStream(envoy_stream_t stream_handle, Client& http_client)
    : stream_handle_(stream_handle), parent_(http_client) {}

Client::DirectStream::~DirectStream() { ENVOY_LOG(debug, "[S{}] destroy stream", stream_handle_); }

CodecEventCallbacks*
Client::DirectStream::registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) {
  // registerCodecEventCallbacks is called with nullptr when the underlying
  // Envoy stream is going away. Make sure Envoy Mobile sees the stream as
  // closed as well.
  if (codec_callbacks == nullptr &&
      parent_.getStream(stream_handle_, GetStreamFilters::AllowOnlyForOpenStreams)) {
    // Generally this only happens if Envoy closes the (virtual) downstream
    // connection, otherwise Envoy would inform the codec to send a reset.
    callbacks_->closeStream(false);
  }
  std::swap(codec_callbacks, codec_callbacks_);
  return codec_callbacks;
}

// Correctly receiving resetStream() for errors in Http::Client relies on at least one filter
// resetting the stream when handling a pending local response. By default, the LocalReplyFilter
// has this responsibility.
void Client::DirectStream::resetStream(StreamResetReason reason) {
  // This seems in line with other codec implementations, and so the assumption is that this is in
  // line with upstream expectations.
  // TODO(goaway): explore an upstream fix to get the HCM to clean up ActiveStream itself.
  saveFinalStreamIntel();   // Take a snapshot now in case the stream gets destroyed.
  callbacks_->latchError(); // Latch the error in case the stream gets destroyed.
  runResetCallbacks(reason, absl::string_view());
  if (!parent_.getStream(stream_handle_, GetStreamFilters::AllowForAllStreams)) {
    // We don't assert here, because Envoy will issue a stream reset if a stream closes remotely
    // while still open locally. In this case the stream will already have been removed from
    // our stream maps due to the remote closure.
    return;
  }
  callbacks_->onError();
}

void Client::DirectStream::readDisable(bool disable) {
  if (disable) {
    ++read_disable_count_;
  } else {
    ASSERT(read_disable_count_ > 0);
    --read_disable_count_;
    if (read_disable_count_ == 0 && wants_write_notification_) {
      wants_write_notification_ = false;
      callbacks_->onSendWindowAvailable();
    }
  }
}

void Client::DirectStream::dumpState(std::ostream&, int indent_level) const {
  // TODO(junr03): output to ostream arg - https://github.com/envoyproxy/envoy-mobile/issues/1497.
  std::stringstream ss;
  const char* spaces = spacesForLevel(indent_level);

  ss << spaces << "DirectStream" << DUMP_MEMBER(stream_handle_) << std::endl;
  ENVOY_LOG(error, "\n{}", ss.str());
}

void Client::startStream(envoy_stream_t new_stream_handle, EnvoyStreamCallbacks&& stream_callbacks,
                         bool explicit_flow_control) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream{new DirectStream(new_stream_handle, *this)};
  direct_stream->explicit_flow_control_ = explicit_flow_control;
  direct_stream->callbacks_ =
      std::make_unique<DirectStreamCallbacks>(*direct_stream, std::move(stream_callbacks), *this);

  // Note: streams created by Envoy Mobile are tagged as is_internally_created. This means that
  // the Http::ConnectionManager _will not_ sanitize headers when creating a stream.
  direct_stream->request_decoder_ =
      api_listener_->newStreamHandle(*direct_stream->callbacks_, true /* is_internally_created */);

  streams_.emplace(new_stream_handle, std::move(direct_stream));
  ENVOY_LOG(debug, "[S{}] start stream", new_stream_handle);
}

void Client::sendHeaders(envoy_stream_t stream, RequestHeaderMapPtr headers, bool end_stream,
                         bool idempotent) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream, GetStreamFilters::AllowOnlyForOpenStreams);
  // If direct_stream is not found, it means the stream has already closed or been reset
  // and the appropriate callback has been issued to the caller. There's nothing to do here
  // except silently swallow this.
  // TODO: handle potential race condition with cancellation or failure get a stream in the
  // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
  // from the caller.
  // https://github.com/envoyproxy/envoy-mobile/issues/301
  if (!direct_stream) {
    return;
  }
  OptRef<RequestDecoder> request_decoder = direct_stream->requestDecoder();
  if (!request_decoder) {
    return;
  }

  ScopeTrackerScopeState scope(direct_stream.get(), scopeTracker());

  // This is largely a check for the android platform: isCleartextPermitted
  // is a no-op for other platforms.
  if (headers->getSchemeValue() != "https" &&
      !SystemHelper::getInstance().isCleartextPermitted(headers->getHostValue())) {
    request_decoder->sendLocalReply(Http::Code::BadRequest, "Cleartext is not permitted", nullptr,
                                    absl::nullopt, "");
    return;
  }

  setDestinationCluster(*headers);
  // Set the x-forwarded-proto header to https because Envoy Mobile only has clusters with TLS
  // enabled. This is done here because the ApiListener's synthetic connection would make the
  // Http::ConnectionManager set the scheme to http otherwise. In the future we might want to
  // configure the connection instead of setting the header here.
  // https://github.com/envoyproxy/envoy/issues/10291
  //
  // Setting this header is also currently important because Envoy Mobile starts stream with the
  // ApiListener setting the is_internally_created bool to true. This means the
  // Http::ConnectionManager *will not* mutate Envoy Mobile's request headers. One of the
  // mutations done is adding the x-forwarded-proto header if not present. Unfortunately, the
  // router relies on the present of this header to determine if it should provided a route for
  // a request here:
  // https://github.com/envoyproxy/envoy/blob/c9e3b9d2c453c7fe56a0e3615f0c742ac0d5e768/source/common/router/config_impl.cc#L1091-L1096
  headers->setReferenceForwardedProto(Headers::get().SchemeValues.Https);
  // When the request is idempotent, it is safe to retry.
  if (idempotent) {
    // https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/router_filter#x-envoy-retry-on
    headers->addCopy(Headers::get().EnvoyRetryOn,
                     Headers::get().EnvoyRetryOnValues.Http3PostConnectFailure);
  }
  ENVOY_LOG(debug, "[S{}] request headers for stream (end_stream={}):\n{}", stream, end_stream,
            *headers);
  request_decoder->decodeHeaders(std::move(headers), end_stream);
}

void Client::readData(envoy_stream_t stream, size_t bytes_to_read) {
  ASSERT(dispatcher_.isThreadSafe());
  // This is allowed for closed streams, else we could never send data up after
  // the FIN was received.
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream, GetStreamFilters::AllowForAllStreams);
  // If direct_stream is not found, it means the stream has already canceled or been reset
  // and the appropriate callback has been issued to the caller. There's nothing to do here
  // except silently swallow this.
  if (direct_stream) {
    direct_stream->callbacks_->resumeData(bytes_to_read);
  }
}

void Client::sendData(envoy_stream_t stream, Buffer::InstancePtr buffer, bool end_stream) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream, GetStreamFilters::AllowOnlyForOpenStreams);

  // If direct_stream is not found, it means the stream has already closed or been reset
  // and the appropriate callback has been issued to the caller. There's nothing to do here
  // except silently swallow this.
  // TODO: handle potential race condition with cancellation or failure get a stream in the
  // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
  // from the caller.
  // https://github.com/envoyproxy/envoy-mobile/issues/301
  if (!direct_stream) {
    return;
  }
  OptRef<RequestDecoder> request_decoder = direct_stream->requestDecoder();
  if (!request_decoder) {
    return;
  }

  ScopeTrackerScopeState scope(direct_stream.get(), scopeTracker());

  ENVOY_LOG(debug, "[S{}] request data for stream (length={} end_stream={})\n", stream,
            buffer->length(), end_stream);
  request_decoder->decodeData(*buffer, end_stream);

  if (direct_stream->explicit_flow_control_ && !end_stream) {
    if (direct_stream->read_disable_count_ == 0) {
      // If there is still buffer space after the write, notify the sender
      // that send window is available, on the next dispatcher iteration so
      // that repeated writes do not starve reads.
      direct_stream->wants_write_notification_ = false;
      // A new callback must be scheduled each time to capture any changes to the
      // DirectStream's callbacks from call to call.
      direct_stream->scheduled_callback_ = dispatcher_.createSchedulableCallback(
          [direct_stream] { direct_stream->callbacks_->onSendWindowAvailable(); });
      direct_stream->scheduled_callback_->scheduleCallbackNextIteration();
    } else {
      // Otherwise, make sure the stack will send a notification when the
      // buffers are drained.
      direct_stream->wants_write_notification_ = true;
    }
  }
}

void Client::sendMetadata(envoy_stream_t, envoy_headers) { IS_ENVOY_BUG("not implemented"); }

void Client::sendTrailers(envoy_stream_t stream, RequestTrailerMapPtr trailers) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream, GetStreamFilters::AllowOnlyForOpenStreams);
  // If direct_stream is not found, it means the stream has already closed or been reset
  // and the appropriate callback has been issued to the caller. There's nothing to do here
  // except silently swallow this.
  // TODO: handle potential race condition with cancellation or failure get a stream in the
  // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
  // from the caller.
  // https://github.com/envoyproxy/envoy-mobile/issues/301
  if (!direct_stream) {
    return;
  }
  OptRef<RequestDecoder> request_decoder = direct_stream->requestDecoder();
  if (!request_decoder) {
    return;
  }
  ScopeTrackerScopeState scope(direct_stream.get(), scopeTracker());
  ENVOY_LOG(debug, "[S{}] request trailers for stream:\n{}", stream, *trailers);
  request_decoder->decodeTrailers(std::move(trailers));
}

void Client::cancelStream(envoy_stream_t stream) {
  ASSERT(dispatcher_.isThreadSafe());
  // This is the one place where downstream->upstream communication is allowed
  // for closed streams: if the client cancels the stream it should be canceled
  // whether it was closed or not.
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream, GetStreamFilters::AllowForAllStreams);
  if (direct_stream) {
    // Attempt to latch the latest stream info. This will be a no-op if the stream
    // is already complete.
    ENVOY_LOG(debug, "[S{}] application cancelled stream", stream);
    direct_stream->saveFinalStreamIntel();
    bool stream_was_open = getStream(stream, GetStreamFilters::AllowOnlyForOpenStreams) != nullptr;
    ScopeTrackerScopeState scope(direct_stream.get(), scopeTracker());
    direct_stream->notifyAdapter(DirectStream::AdapterSignal::Cancel);
    removeStream(direct_stream->stream_handle_);

    direct_stream->callbacks_->onCancel();

    // Since https://github.com/envoyproxy/envoy/pull/13052, the connection manager expects that
    // response code details are set on all possible paths for streams.
    direct_stream->setResponseDetails(getCancelDetails());

    // Only run the reset callback if the stream is still open.
    if (stream_was_open) {
      // The runResetCallbacks call synchronously causes Envoy to defer delete the HCM's
      // ActiveStream. We have some concern that this could potentially race a terminal callback
      // scheduled on the same iteration of the event loop. If we see violations in the callback
      // assertions checking stream presence, this is a likely potential culprit. However, it's
      // plausible that upstream guards will protect us here, given that Envoy allows streams to be
      // reset from a wide variety of contexts without apparent issue.
      direct_stream->runResetCallbacks(StreamResetReason::RemoteReset, absl::string_view());
    }
  }
}

const HttpClientStats& Client::stats() const { return stats_; }

Client::DirectStreamSharedPtr Client::getStream(envoy_stream_t stream,
                                                GetStreamFilters get_stream_filters) {
  auto direct_stream_pair_it = streams_.find(stream);
  if (direct_stream_pair_it != streams_.end()) {
    return direct_stream_pair_it->second;
  }
  if (get_stream_filters == GetStreamFilters::AllowForAllStreams) {
    direct_stream_pair_it = closed_streams_.find(stream);
    if (direct_stream_pair_it != closed_streams_.end()) {
      return direct_stream_pair_it->second;
    }
  }
  return nullptr;
}

void Client::removeStream(envoy_stream_t stream_handle) {
  RELEASE_ASSERT(
      dispatcher_.isThreadSafe(),
      fmt::format("[S{}] stream removeStream must be performed on the dispatcher_'s thread.",
                  stream_handle));
  Client::DirectStreamSharedPtr direct_stream =
      getStream(stream_handle, GetStreamFilters::AllowForAllStreams);
  RELEASE_ASSERT(
      direct_stream,
      fmt::format(
          "[S{}] removeStream is a private method that is only called with stream ids that exist",
          stream_handle));

  direct_stream->scheduled_callback_ = nullptr;
  // The DirectStream should live through synchronous code that already has a reference to it.
  // Hence why it is scheduled for deferred deletion. If this was all that was needed then it
  // would be sufficient to return a shared_ptr in getStream. However, deferred deletion is still
  // required because in Client::resetStream the DirectStream needs to live for as long and
  // the HCM's ActiveStream lives. Hence its deletion needs to live beyond the synchronous code in
  // Client::resetStream.
  auto direct_stream_wrapper = std::make_unique<DirectStreamWrapper>(std::move(direct_stream));
  dispatcher_.deferredDelete(std::move(direct_stream_wrapper));
  // However, the entry in the map should not exist after removeStream.
  // Hence why it is synchronously erased from the streams map.
  size_t erased = streams_.erase(stream_handle);
  if (erased != 1) {
    erased = closed_streams_.erase(stream_handle);
  }
  ASSERT(erased == 1, "removeStream should always remove one entry from the streams map");
  ENVOY_LOG(debug, "[S{}] erased stream from streams container", stream_handle);
}

namespace {

const LowerCaseString ClusterHeader{"x-envoy-mobile-cluster"};

const char* BaseCluster = "base";
const char* ClearTextCluster = "base_clear";

} // namespace

void Client::setDestinationCluster(Http::RequestHeaderMap& headers) {
  // Determine upstream cluster:
  // - Force http/1.1 if request scheme is http (cleartext).
  // - Base cluster (best available protocol) for all secure traffic.
  const char* cluster{};
  if (headers.getSchemeValue() == Headers::get().SchemeValues.Http) {
    cluster = ClearTextCluster;
  } else {
    cluster = BaseCluster;
  }

  headers.addCopy(ClusterHeader, std::string{cluster});
}

} // namespace Http
} // namespace Envoy
