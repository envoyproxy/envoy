#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "extensions/filters/http/ext_proc/mutation_utils.h"

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
using Http::RequestHeaderMap;
using Http::ResponseHeaderMap;

static const std::string kErrorPrefix = "ext_proc error";

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
  // Make doubly-sure we no longer use the stream, as
  // per the filter contract.
  processing_complete_ = true;
  if (stream_) {
    if (stream_->close()) {
      stats_.streams_closed_.inc();
    }
  }
}

FilterHeadersStatus Filter::onHeaders(ProcessorState& state, Http::HeaderMap& headers,
                                      bool end_stream) {
  ENVOY_BUG(state.callbacksIdle(), "Invalid filter state");

  // We're at the start, so start the stream and send a headers message
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
  ProcessingRequest req;
  auto* headers_req = state.mutableHeaders(req);
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  state.setCallbackState(ProcessorState::CallbackState::Headers);
  state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout());
  ENVOY_LOG(debug, "Sending headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);

  if (processing_mode_.request_header_mode() == ProcessingMode::SKIP) {
    ENVOY_LOG(trace, "decodeHeaders: Skipped");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(decoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "decodeHeaders returning {}", status);
  return status;
}

Http::FilterDataStatus Filter::onData(ProcessorState& state, ProcessingMode::BodySendMode body_mode,
                                      Buffer::Instance& data, bool end_stream) {
  switch (body_mode) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      ENVOY_BUG(state.callbacksIdle(), "Invalid filter state");
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
      state.addBufferedData(data);
      state.setCallbackState(ProcessorState::CallbackState::BufferedBody);
      state.startMessageTimer(std::bind(&Filter::onMessageTimeout, this),
                              config_->messageTimeout());
      sendBodyChunk(state, *state.bufferedData(), true);
    } else {
      ENVOY_LOG(trace, "onData: Buffering");
    }
    return FilterDataStatus::StopIterationAndBuffer;
  case ProcessingMode::BUFFERED_PARTIAL:
  case ProcessingMode::STREAMED:
    ENVOY_LOG(debug, "Ignoring unimplemented request body processing mode");
    return FilterDataStatus::Continue;
  case ProcessingMode::NONE:
  default:
    return FilterDataStatus::Continue;
  }
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData({}): end_stream = {}", data.length(), end_stream);
  if (processing_complete_) {
    ENVOY_LOG(trace, "decodeData: Continue (complete)");
    return FilterDataStatus::Continue;
  }

  const auto status =
      onData(decoding_state_, processing_mode_.request_body_mode(), data, end_stream);
  ENVOY_LOG(trace, "decodeData returning {}", status);
  return status;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "encodeHeaders end_stream = {}", end_stream);

  if (processing_complete_ || processing_mode_.response_header_mode() == ProcessingMode::SKIP) {
    ENVOY_LOG(trace, "encodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(encoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "encodeHeaders returns {}", status);
  return status;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData({}): end_stream = {}", data.length(), end_stream);
  if (processing_complete_) {
    ENVOY_LOG(trace, "encodeData: Continue (complete)");
    return FilterDataStatus::Continue;
  }

  const auto status =
      onData(encoding_state_, processing_mode_.response_body_mode(), data, end_stream);
  ENVOY_LOG(trace, "encodeData returning {}", status);
  return status;
}

void Filter::sendBodyChunk(const ProcessorState& state, const Buffer::Instance& data,
                           bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes", data.length());
  ProcessingRequest req;
  auto* body_req = state.mutableBody(req);
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  if (processing_complete_) {
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
    processing_mode_ = response->mode_override();
  }

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
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    // We won't be sending anything more to the stream after we
    // receive this message.
    processing_complete_ = true;
    sendImmediateResponse(response->immediate_response());
    message_handled = true;
    break;
  default:
    // Any other message is considered spurious
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
  const auto status_code = response.has_status() ? response.status().code() : 200;
  const auto grpc_status =
      response.has_grpc_status()
          ? absl::optional<Grpc::Status::GrpcStatus>(response.grpc_status().status())
          : absl::nullopt;
  const auto mutate_headers = [&response](Http::ResponseHeaderMap& headers) {
    if (response.has_headers()) {
      MutationUtils::applyHeaderMutations(response.headers(), headers);
    }
  };

  sent_immediate_response_ = true;
  encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                     mutate_headers, grpc_status, response.details());
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
