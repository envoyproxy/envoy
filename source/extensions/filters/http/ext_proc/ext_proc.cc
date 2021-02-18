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

void Filter::openStream() {
  if (!stream_) {
    ENVOY_LOG(debug, "Opening gRPC stream to external processor");
    stream_ = client_->start(*this, config_->grpcTimeout());
    stats_.streams_started_.inc();
  }
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
  openStream();
  request_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_request_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  request_state_ = FilterState::Headers;
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
      decoder_callbacks_->addDecodedData(data, false);
      // The body has been buffered and we need to send the buffer
      request_state_ = FilterState::BufferedBody;
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
  openStream();
  response_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_response_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  response_state_ = FilterState::Headers;
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
      encoder_callbacks_->addEncodedData(data, false);
      response_state_ = FilterState::BufferedBody;
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
  openStream();
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
    handleImmediateResponse(response->immediate_response());
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
    cleanupState();
    processing_complete_ = true;
  }
}

bool Filter::handleRequestHeadersResponse(const HeadersResponse& response) {
  if (request_state_ == FilterState::Headers) {
    ENVOY_LOG(debug, "applying request_headers response");
    MutationUtils::applyCommonHeaderResponse(response, *request_headers_);
    request_headers_ = nullptr;
    request_state_ = FilterState::Idle;
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
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

void Filter::handleImmediateResponse(const ImmediateResponse& response) {
  // We don't want to process any more stream messages after this.
  // Close the stream before sending because "sendLocalResponse" may trigger
  // additional calls to this filter.
  processing_complete_ = true;
  sendImmediateResponse(response);
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stats_.streams_failed_.inc();

  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();

  } else {
    processing_complete_ = true;
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s: gRPC error %i", kErrorPrefix, status));
    handleImmediateResponse(errorResponse);
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");
  processing_complete_ = true;
  stats_.streams_closed_.inc();
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  cleanupState();
}

void Filter::cleanupState() {
  if (request_state_ != FilterState::Idle) {
    request_state_ = FilterState::Idle;
    decoder_callbacks_->continueDecoding();
  }
  if (response_state_ != FilterState::Idle) {
    response_state_ = FilterState::Idle;
    encoder_callbacks_->continueEncoding();
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

  encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                     mutate_headers, grpc_status, response.details());
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy