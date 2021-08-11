#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;

using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::RequestHeaderMap;
using Http::RequestTrailerMap;
using Http::ResponseHeaderMap;
using Http::ResponseTrailerMap;

static const std::string kErrorPrefix = "ext_proc error";
static const int DefaultImmediateStatus = 200;

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  decoding_state_.setDecoderFilterCallbacks(callbacks);
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setEncoderFilterCallbacks(callbacks);
  encoding_state_.setEncoderFilterCallbacks(callbacks);
}

Filter::StreamOpenState Filter::openStream() {
  ENVOY_BUG(!processing_complete_, "openStream should not have been called");
  if (!stream_) {
    ENVOY_LOG(debug, "Opening gRPC stream to external processor");
    stream_ = client_->start(*this);
    stats_.streams_started_.inc();
    if (processing_complete_) {
      // Stream failed while starting and either onGrpcError or onGrpcClose was already called
      return sent_immediate_response_ ? StreamOpenState::Error : StreamOpenState::IgnoreError;
    }
  }
  return StreamOpenState::Ok;
}

void Filter::onDestroy() {
  ENVOY_LOG(trace, "onDestroy");
  // Make doubly-sure we no longer use the stream, as
  // per the filter contract.
  processing_complete_ = true;
  if (stream_) {
    if (stream_->close()) {
      stats_.streams_closed_.inc();
    }
  }
}

FilterHeadersStatus Filter::onHeaders(ProcessorState& state,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream) {
  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterHeadersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterHeadersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  state.setHeaders(&headers);
  state.setHasNoBody(end_stream);
  ProcessingRequest req;
  auto* headers_req = state.mutableHeaders(req);
  MutationUtils::headersToProto(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  state.setCallbackState(ProcessorState::CallbackState::HeadersCallback);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ENVOY_LOG(debug, "Sending headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  state.setPaused(true);
  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  if (end_stream) {
    decoding_state_.setCompleteBodyAvailable(true);
  }

  if (!decoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "decodeHeaders: Skipped");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(decoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "decodeHeaders returning {}", status);
  return status;
}

FilterDataStatus Filter::onData(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    state.setCompleteBodyAvailable(true);
  }
  if (state.bodyReplaced()) {
    ENVOY_LOG(trace, "Clearing body chunk because CONTINUE_AND_REPLACE was returned");
    data.drain(data.length());
    return FilterDataStatus::Continue;
  }
  if (processing_complete_) {
    ENVOY_LOG(trace, "Continuing (processing complete)");
    return FilterDataStatus::Continue;
  }
  bool just_added_trailers = false;
  Http::HeaderMap* new_trailers = nullptr;
  if (end_stream && state.sendTrailers()) {
    // We're at the end of the stream, but the filter wants to process trailers.
    // According to the filter contract, this is the only place where we can
    // add trailers, even if we will return right after this and process them
    // later.
    ENVOY_LOG(trace, "Creating new, empty trailers");
    new_trailers = state.addTrailers();
    state.setTrailersAvailable(true);
    just_added_trailers = true;
  }
  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback) {
    ENVOY_LOG(trace, "Header processing still in progress -- holding body data");
    // We don't know what to do with the body until the response comes back.
    // We must buffer it in case we need it when that happens.
    if (end_stream) {
      state.setPaused(true);
      return FilterDataStatus::StopIterationAndBuffer;
    } else {
      // Raise a watermark to prevent a buffer overflow until the response comes back.
      state.setPaused(true);
      state.requestWatermark();
      return FilterDataStatus::StopIterationAndWatermark;
    }
  }
  if (state.callbackState() == ProcessorState::CallbackState::StreamedBodyCallbackFinishing) {
    // We were previously streaming the body, but there are more chunks waiting
    // to be processed, so we can't send the body yet.
    // Move the data for our chunk into a queue so that we can re-inject it later
    // when the processor returns. See the comments below for more details on how
    // this works in general.
    ENVOY_LOG(trace, "Enqueuing data while we wait for processing to finish");
    state.enqueueStreamingChunk(data, end_stream, false);
    if (end_stream) {
      // But we need to buffer the last chunk because it's our last chance to do stuff
      state.setPaused(true);
      return FilterDataStatus::StopIterationNoBuffer;
    } else {
      return FilterDataStatus::Continue;
    }
  }

  FilterDataStatus result;
  switch (state.bodyMode()) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      switch (openStream()) {
      case StreamOpenState::Error:
        return FilterDataStatus::StopIterationNoBuffer;
      case StreamOpenState::IgnoreError:
        return FilterDataStatus::Continue;
      case StreamOpenState::Ok:
        // Fall through
        break;
      }

      // The body has been buffered and we need to send the buffer
      ENVOY_LOG(debug, "Sending request body message");
      state.addBufferedData(data);
      sendBodyChunk(state, *state.bufferedData(),
                    ProcessorState::CallbackState::BufferedBodyCallback, true);
      // Since we just just moved the data into the buffer, return NoBuffer
      // so that we do not buffer this chunk twice.
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
      break;
    }
    ENVOY_LOG(trace, "onData: Buffering");
    state.setPaused(true);
    result = FilterDataStatus::StopIterationAndBuffer;
    break;
  case ProcessingMode::STREAMED: {
    // STREAMED body mode works as follows:
    //
    // 1) As data callbacks come in to the filter, it "moves" the data into a new buffer, which it
    // dispatches via gRPC message to the external processor, and then keeps in a queue. It
    // may request a watermark if the queue is higher than the buffer limit to prevent running
    // out of memory.
    // 2) As a result, filters farther down the chain see empty buffers in some data callbacks.
    // 3) When a response comes back from the external processor, it injects the processor's result
    // into the filter chain using "inject**codedData". (The processor may respond indicating that
    // there is no change, which means that the original buffer stored in the queue is what gets
    // injected.)
    //
    // This way, we pipeline data from the proxy to the external processor, and give the processor
    // the ability to modify each chunk, in order. Doing this any other way would have required
    // substantial changes to the filter manager. See
    // https://github.com/envoyproxy/envoy/issues/16760 for a discussion.
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }
    // Send the chunk on the gRPC stream
    sendBodyChunk(state, data, ProcessorState::CallbackState::StreamedBodyCallback, end_stream);
    // Move the data to the queue and optionally raise the watermark.
    state.enqueueStreamingChunk(data, end_stream, true);

    // At this point we will continue, but with no data, because that will come later
    if (end_stream) {
      // But we need to stop iteration for the last chunk because it's our last chance to do stuff
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
    } else {
      result = FilterDataStatus::Continue;
    }
    break;
  }
  case ProcessingMode::BUFFERED_PARTIAL:
    // BUFFERED_PARTIAL mode works as follows:
    //
    // 1) As data chunks arrive, we move the data into a new buffer, which we store
    // in the buffer queue, and continue the filter stream with an empty buffer. This
    // is the same thing that we do in STREAMING mode.
    // 2) If end of stream is reached before the queue reaches the buffer limit, we
    // send the buffered data to the server and essentially behave as if we are in
    // buffered mode.
    // 3) If instead the buffer limit is reached before end of stream, then we also
    // send the buffered data to the server, and raise the watermark to prevent Envoy
    // from running out of memory while we wait.
    // 4) It is possible that Envoy will keep sending us data even in that case, so
    // we must continue to queue data and prepare to re-inject it later.
    if (state.partialBodyProcessed()) {
      // We already sent and received the buffer, so everything else just falls through.
      ENVOY_LOG(trace, "Partial buffer limit reached");
      result = FilterDataStatus::Continue;
    } else if (state.callbackState() ==
               ProcessorState::CallbackState::BufferedPartialBodyCallback) {
      // More data came in while we were waiting for a callback result. We need
      // to queue it and deliver it later in case the callback changes the data.
      state.enqueueStreamingChunk(data, false, false);
      ENVOY_LOG(trace, "Call in progress for partial mode");
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
    } else if (end_stream || state.queueOverHighLimit()) {
      switch (openStream()) {
      case StreamOpenState::Error:
        return FilterDataStatus::StopIterationNoBuffer;
      case StreamOpenState::IgnoreError:
        return FilterDataStatus::Continue;
      case StreamOpenState::Ok:
        // Fall through
        break;
      }
      state.enqueueStreamingChunk(data, false, false);
      // Put all buffered data so far into one big buffer
      const auto& all_data = state.consolidateStreamedChunks(true);
      ENVOY_LOG(debug, "Sending {} bytes of data in buffered partial mode. end_stream = {}",
                all_data.data.length(), end_stream);
      sendBodyChunk(state, all_data.data,
                    ProcessorState::CallbackState::BufferedPartialBodyCallback, end_stream);
      result = FilterDataStatus::StopIterationNoBuffer;
      state.setPaused(true);
    } else {
      // Keep on running and buffering
      state.enqueueStreamingChunk(data, false, false);
      result = FilterDataStatus::Continue;
    }
    break;
  case ProcessingMode::NONE:
  default:
    result = FilterDataStatus::Continue;
    break;
  }
  if (just_added_trailers) {
    // If we get here, then we need to send the trailers message now
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }
    sendTrailers(state, *new_trailers);
    state.setPaused(true);
    return FilterDataStatus::StopIterationAndBuffer;
  }
  return result;
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(decoding_state_, data, end_stream);
  ENVOY_LOG(trace, "decodeData returning {}", status);
  return status;
}

FilterTrailersStatus Filter::onTrailers(ProcessorState& state, Http::HeaderMap& trailers) {
  if (processing_complete_) {
    ENVOY_LOG(trace, "trailers: Continue");
    return FilterTrailersStatus::Continue;
  }

  bool body_delivered = state.completeBodyAvailable();
  state.setCompleteBodyAvailable(true);
  state.setTrailersAvailable(true);
  state.setTrailers(&trailers);

  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback ||
      state.callbackState() == ProcessorState::CallbackState::BufferedBodyCallback) {
    ENVOY_LOG(trace, "Previous callback still executing -- holding header iteration");
    state.setPaused(true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!body_delivered && state.bodyMode() == ProcessingMode::BUFFERED) {
    // We would like to process the body in a buffered way, but until now the complete
    // body has not arrived. With the arrival of trailers, we now know that the body
    // has arrived.
    sendBufferedData(state, ProcessorState::CallbackState::BufferedBodyCallback, true);
    state.setPaused(true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!state.sendTrailers()) {
    ENVOY_LOG(trace, "Skipped trailer processing");
    return FilterTrailersStatus::Continue;
  }

  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterTrailersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterTrailersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  sendTrailers(state, trailers);
  state.setPaused(true);
  return FilterTrailersStatus::StopIteration;
}

FilterTrailersStatus Filter::decodeTrailers(RequestTrailerMap& trailers) {
  ENVOY_LOG(trace, "decodeTrailers");
  const auto status = onTrailers(decoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", status);
  return status;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "encodeHeaders end_stream = {}", end_stream);
  if (end_stream) {
    encoding_state_.setCompleteBodyAvailable(true);
  }

  if (processing_complete_ || !encoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "encodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(encoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "encodeHeaders returns {}", status);
  return status;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(encoding_state_, data, end_stream);
  ENVOY_LOG(trace, "encodeData returning {}", status);
  return status;
}

FilterTrailersStatus Filter::encodeTrailers(ResponseTrailerMap& trailers) {
  ENVOY_LOG(trace, "encodeTrailers");
  const auto status = onTrailers(encoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", status);
  return status;
}

void Filter::sendBodyChunk(ProcessorState& state, const Buffer::Instance& data,
                           ProcessorState::CallbackState new_state, bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes", data.length());
  state.setCallbackState(new_state);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ProcessingRequest req;
  auto* body_req = state.mutableBody(req);
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers) {
  ProcessingRequest req;
  auto* trailers_req = state.mutableTrailers(req);
  MutationUtils::headersToProto(trailers, *trailers_req->mutable_trailers());
  state.setCallbackState(ProcessorState::CallbackState::TrailersCallback);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ENVOY_LOG(debug, "Sending trailers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  if (processing_complete_) {
    ENVOY_LOG(debug, "Ignoring stream message received after processing complete");
    // Ignore additional messages after we decided we were done with the stream
    return;
  }

  auto response = std::move(r);
  bool message_handled = false;

  // Update processing mode now because filter callbacks check it
  // and the various "handle" methods below may result in callbacks
  // being invoked in line.
  if (response->has_mode_override()) {
    ENVOY_LOG(debug, "Processing mode overridden by server for this request");
    decoding_state_.setProcessingMode(response->mode_override());
    encoding_state_.setProcessingMode(response->mode_override());
  }

  ENVOY_LOG(debug, "Received {} response", responseCaseToString(response->response_case()));
  switch (response->response_case()) {
  case ProcessingResponse::ResponseCase::kRequestHeaders:
    message_handled = decoding_state_.handleHeadersResponse(response->request_headers());
    break;
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    message_handled = encoding_state_.handleHeadersResponse(response->response_headers());
    break;
  case ProcessingResponse::ResponseCase::kRequestBody:
    message_handled = decoding_state_.handleBodyResponse(response->request_body());
    break;
  case ProcessingResponse::ResponseCase::kResponseBody:
    message_handled = encoding_state_.handleBodyResponse(response->response_body());
    break;
  case ProcessingResponse::ResponseCase::kRequestTrailers:
    message_handled = decoding_state_.handleTrailersResponse(response->request_trailers());
    break;
  case ProcessingResponse::ResponseCase::kResponseTrailers:
    message_handled = encoding_state_.handleTrailersResponse(response->response_trailers());
    break;
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    // We won't be sending anything more to the stream after we
    // receive this message.
    processing_complete_ = true;
    cleanUpTimers();
    sendImmediateResponse(response->immediate_response());
    message_handled = true;
    break;
  default:
    // Any other message is considered spurious
    ENVOY_LOG(debug, "Received unknown stream message {} -- ignoring and marking spurious",
              response->response_case());
    break;
  }

  if (message_handled) {
    stats_.stream_msgs_received_.inc();
  } else {
    stats_.spurious_msgs_received_.inc();
    // When a message is received out of order, ignore it and also
    // ignore the stream for the rest of this filter instance's lifetime
    // to protect us from a malformed server.
    ENVOY_LOG(warn, "Spurious response message {} received on gRPC stream",
              response->response_case());
    clearAsyncState();
    processing_complete_ = true;
  }
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stats_.streams_failed_.inc();

  if (processing_complete_) {
    return;
  }

  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();

  } else {
    processing_complete_ = true;
    // Since the stream failed, there is no need to handle timeouts, so
    // make sure that they do not fire now.
    cleanUpTimers();
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: gRPC error %i", kErrorPrefix, status));
    sendImmediateResponse(errorResponse);
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");
  processing_complete_ = true;
  stats_.streams_closed_.inc();
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  clearAsyncState();
}

void Filter::onMessageTimeout() {
  ENVOY_LOG(debug, "message timeout reached");
  stats_.message_timeouts_.inc();
  if (config_->failureModeAllow()) {
    // The user would like a timeout to not cause message processing to fail.
    // However, we don't know if the external processor will send a response later,
    // and we can't wait any more. So, as we do for a spurious message, ignore
    // the external processor for the rest of the request.
    processing_complete_ = true;
    stats_.failure_mode_allowed_.inc();
    clearAsyncState();

  } else {
    // Return an error and stop processing the current stream.
    processing_complete_ = true;
    decoding_state_.setCallbackState(ProcessorState::CallbackState::Idle);
    encoding_state_.setCallbackState(ProcessorState::CallbackState::Idle);
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: per-message timeout exceeded", kErrorPrefix));
    sendImmediateResponse(errorResponse);
  }
}

// Regardless of the current filter state, reset it to "IDLE", continue
// the current callback, and reset timers. This is used in a few error-handling situations.
void Filter::clearAsyncState() {
  decoding_state_.clearAsyncState();
  encoding_state_.clearAsyncState();
}

// Regardless of the current state, ensure that the timers won't fire
// again.
void Filter::cleanUpTimers() {
  decoding_state_.cleanUpTimer();
  encoding_state_.cleanUpTimer();
}

void Filter::sendImmediateResponse(const ImmediateResponse& response) {
  auto status_code = response.has_status() ? response.status().code() : DefaultImmediateStatus;
  if (!MutationUtils::isValidHttpStatus(status_code)) {
    ENVOY_LOG(debug, "Ignoring attempt to set invalid HTTP status {}", status_code);
    status_code = DefaultImmediateStatus;
  }
  const auto grpc_status =
      response.has_grpc_status()
          ? absl::optional<Grpc::Status::GrpcStatus>(response.grpc_status().status())
          : absl::nullopt;
  const auto mutate_headers = [&response](Http::ResponseHeaderMap& headers) {
    if (response.has_headers()) {
      MutationUtils::applyHeaderMutations(response.headers(), headers, false);
    }
  };

  sent_immediate_response_ = true;
  ENVOY_LOG(debug, "Sending local reply with status code {}", status_code);
  encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                     mutate_headers, grpc_status, response.details());
}

std::string responseCaseToString(const ProcessingResponse::ResponseCase response_case) {
  switch (response_case) {
  case ProcessingResponse::ResponseCase::kRequestHeaders:
    return "request headers";
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    return "response headers";
  case ProcessingResponse::ResponseCase::kRequestBody:
    return "request body";
  case ProcessingResponse::ResponseCase::kResponseBody:
    return "response body";
  case ProcessingResponse::ResponseCase::kRequestTrailers:
    return "request trailers";
  case ProcessingResponse::ResponseCase::kResponseTrailers:
    return "response trailers";
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    return "immediate response";
  default:
    return "unknown";
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
