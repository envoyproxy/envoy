#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;

using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::RequestHeaderMap;
using Http::ResponseHeaderMap;

static const std::string kErrorPrefix = "ext_proc error";

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

void Filter::startMessageTimer(Event::TimerPtr& timer) {
  if (!timer) {
    timer =
        decoder_callbacks_->dispatcher().createTimer(std::bind(&Filter::onMessageTimeout, this));
  }
  timer->enableTimer(config_->messageTimeout());
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

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  ENVOY_BUG(request_state_ == FilterState::Idle, "Invalid filter state on request path");

  if (processing_mode_.request_header_mode() == ProcessingMode::SKIP) {
    ENVOY_LOG(trace, "decodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

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

  request_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_request_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  request_state_ = FilterState::Headers;
  startMessageTimer(request_message_timer_);
  ENVOY_LOG(debug, "Sending request_headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();

  // Wait until we have a gRPC response before allowing any more callbacks
  ENVOY_LOG(trace, "decodeHeaders: StopAllIterationAndWatermark");
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData({}): end_stream = {}", data.length(), end_stream);
  if (processing_complete_) {
    ENVOY_LOG(trace, "decodeData: Continue (complete)");
    return FilterDataStatus::Continue;
  }

  switch (processing_mode_.request_body_mode()) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      ENVOY_BUG(request_state_ == FilterState::Idle, "Invalid filter state on request path");
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
      decoder_callbacks_->addDecodedData(data, false);
      request_state_ = FilterState::BufferedBody;
      startMessageTimer(request_message_timer_);
      sendBodyChunk(true, *decoder_callbacks_->decodingBuffer(), true);
    } else {
      ENVOY_LOG(trace, "decodeData: Buffering");
    }
    return FilterDataStatus::StopIterationAndBuffer;
  case ProcessingMode::BUFFERED_PARTIAL:
  case ProcessingMode::STREAMED:
    ENVOY_LOG(debug, "Ignoring unimplemented request body processing mode");
    return FilterDataStatus::Continue;
  case ProcessingMode::NONE:
  default:
    ENVOY_LOG(trace, "decodeData: Skipped");
    return FilterDataStatus::Continue;
  }
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "encodeHeaders end_stream = {}", end_stream);
  ENVOY_BUG(response_state_ == FilterState::Idle, "Invalid filter state on  response path");

  if (processing_complete_ || processing_mode_.response_header_mode() == ProcessingMode::SKIP) {
    ENVOY_LOG(trace, "encodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

  // Depending on processing mode this may or may not be the first message
  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterHeadersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterHeadersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  response_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_response_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  response_state_ = FilterState::Headers;
  startMessageTimer(response_message_timer_);
  ENVOY_LOG(debug, "Sending response_headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  ENVOY_LOG(trace, "encodeHeaders: StopAllIterationAndWatermark");
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData({}): end_stream = {}", data.length(), end_stream);
  if (processing_complete_) {
    ENVOY_LOG(trace, "encodeData: Continue (complete)");
    return FilterDataStatus::Continue;
  }

  switch (processing_mode_.response_body_mode()) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      ENVOY_BUG(response_state_ == FilterState::Idle, "Invalid filter state on response path");
      switch (openStream()) {
      case StreamOpenState::Error:
        return FilterDataStatus::StopIterationNoBuffer;
      case StreamOpenState::IgnoreError:
        return FilterDataStatus::Continue;
      case StreamOpenState::Ok:
        // Fall through
        break;
      }

      encoder_callbacks_->addEncodedData(data, false);
      response_state_ = FilterState::BufferedBody;
      startMessageTimer(response_message_timer_);
      sendBodyChunk(false, *encoder_callbacks_->encodingBuffer(), true);
    } else {
      ENVOY_LOG(trace, "encodeData: Buffering");
    }
    return FilterDataStatus::StopIterationAndBuffer;
  case ProcessingMode::BUFFERED_PARTIAL:
  case ProcessingMode::STREAMED:
    ENVOY_LOG(debug, "Ignoring unimplemented response body processing mode");
    return FilterDataStatus::Continue;
  case ProcessingMode::NONE:
  default:
    ENVOY_LOG(trace, "encodeData: Skipped");
    return FilterDataStatus::Continue;
  }
}

void Filter::sendBodyChunk(bool request_path, const Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes", data.length());
  ProcessingRequest req;
  auto* body_req = request_path ? req.mutable_request_body() : req.mutable_response_body();
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
    message_handled = handleRequestHeadersResponse(response->request_headers());
    break;
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    message_handled = handleResponseHeadersResponse(response->response_headers());
    break;
  case ProcessingResponse::ResponseCase::kRequestBody:
    message_handled = handleRequestBodyResponse(response->request_body());
    break;
  case ProcessingResponse::ResponseCase::kResponseBody:
    message_handled = handleResponseBodyResponse(response->response_body());
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

bool Filter::handleRequestHeadersResponse(const HeadersResponse& response) {
  if (request_state_ == FilterState::Headers) {
    ENVOY_LOG(debug, "applying request_headers response");
    MutationUtils::applyCommonHeaderResponse(response, *request_headers_);
    request_headers_ = nullptr;
    request_state_ = FilterState::Idle;
    request_message_timer_->disableTimer();
    decoder_callbacks_->continueDecoding();
    return true;
  }
  return false;
}

bool Filter::handleResponseHeadersResponse(const HeadersResponse& response) {
  if (response_state_ == FilterState::Headers) {
    ENVOY_LOG(debug, "applying response_headers response");
    MutationUtils::applyCommonHeaderResponse(response, *response_headers_);
    response_headers_ = nullptr;
    response_state_ = FilterState::Idle;
    response_message_timer_->disableTimer();
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

bool Filter::handleRequestBodyResponse(const BodyResponse& response) {
  if (request_state_ == FilterState::BufferedBody) {
    ENVOY_LOG(debug, "Applying request_body response to buffered data");
    decoder_callbacks_->modifyDecodingBuffer([&response](Buffer::Instance& data) {
      MutationUtils::applyCommonBodyResponse(response, data);
    });
    request_state_ = FilterState::Idle;
    request_message_timer_->disableTimer();
    decoder_callbacks_->continueDecoding();
    return true;
  }
  return false;
}

bool Filter::handleResponseBodyResponse(const BodyResponse& response) {
  if (response_state_ == FilterState::BufferedBody) {
    ENVOY_LOG(debug, "Applying response_body response to buffered data");
    ENVOY_LOG(trace, "Buffered data is now {} bytes",
              encoder_callbacks_->encodingBuffer()->length());
    encoder_callbacks_->modifyEncodingBuffer([&response](Buffer::Instance& data) {
      MutationUtils::applyCommonBodyResponse(response, data);
    });
    response_state_ = FilterState::Idle;
    response_message_timer_->disableTimer();
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
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
    request_state_ = FilterState::Idle;
    response_state_ = FilterState::Idle;
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: per-message timeout exceeded", kErrorPrefix));
    sendImmediateResponse(errorResponse);
  }
}

// Regardless of the current filter state, reset it to "IDLE", continue
// the current callback, and reset timers. This is used in a few error-handling situations.
void Filter::clearAsyncState() {
  if (request_state_ != FilterState::Idle) {
    request_state_ = FilterState::Idle;
    decoder_callbacks_->continueDecoding();
  }
  if (response_state_ != FilterState::Idle) {
    response_state_ = FilterState::Idle;
    encoder_callbacks_->continueEncoding();
  }
  cleanUpTimers();
}

// Regardless of the current state, ensure that the timers won't fire
// again.
void Filter::cleanUpTimers() {
  if (request_message_timer_ && request_message_timer_->enabled()) {
    request_message_timer_->disableTimer();
  }
  if (response_message_timer_ && response_message_timer_->enabled()) {
    response_message_timer_->disableTimer();
  }
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