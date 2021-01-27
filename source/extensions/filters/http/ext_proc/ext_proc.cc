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

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_of_stream) {
  if (processing_mode_.request_header_mode() == ProcessingMode::SKIP) {
    return FilterHeadersStatus::Continue;
  }

  // We're at the start, so start the stream and send a headers message
  ENVOY_BUG(request_state_ == FilterState::IDLE, "Filter request state should be idle");
  openStream();
  request_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_request_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_of_stream);
  request_state_ = FilterState::HEADERS;
  ENVOY_LOG(debug, "Sending request_headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();

  // Wait until we have a gRPC response before allowing any more callbacks
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (processing_complete_) {
    return FilterDataStatus::Continue;
  }

  switch (processing_mode_.request_body_mode()) {
  case ProcessingMode::BUFFERED:
  case ProcessingMode::BUFFERED_PARTIAL:
    ENVOY_LOG(warn, "Ignoring unimplemented request body processing mode BUFFERED");
    return FilterDataStatus::Continue;
  case ProcessingMode::STREAMED:
    return decodeDataStreamed(data, end_stream);
  case ProcessingMode::NONE:
  default:
    return FilterDataStatus::Continue;
  }
}

FilterDataStatus Filter::decodeDataStreamed(Buffer::Instance& data, bool end_stream) {
  ENVOY_BUG(request_state_ == FilterState::IDLE, "Filter request state should be idle");
  openStream();
  request_body_chunk_ = &data;
  ProcessingRequest req;
  auto* body_req = req.mutable_request_body();
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  request_state_ = FilterState::BODY;
  ENVOY_LOG(debug, "Sending request_body message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterDataStatus::StopIterationAndWatermark;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_of_stream) {
  if (processing_complete_ || processing_mode_.response_header_mode() == ProcessingMode::SKIP) {
    return FilterHeadersStatus::Continue;
  }

  // Depending on processing mode this may or may not be the first message
  ENVOY_BUG(response_state_ == FilterState::IDLE, "Filter response state should be idle");
  openStream();
  response_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_response_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_of_stream);
  response_state_ = FilterState::HEADERS;
  ENVOY_LOG(debug, "Sending response_headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (processing_complete_) {
    return FilterDataStatus::Continue;
  }

  switch (processing_mode_.response_body_mode()) {
  case ProcessingMode::BUFFERED:
  case ProcessingMode::BUFFERED_PARTIAL:
    ENVOY_LOG(warn, "Ignoring unimplemented response body processing mode BUFFERED");
    return FilterDataStatus::Continue;
  case ProcessingMode::STREAMED:
    return encodeDataStreamed(data, end_stream);
  case ProcessingMode::NONE:
  default:
    return FilterDataStatus::Continue;
  }
}

FilterDataStatus Filter::encodeDataStreamed(Buffer::Instance& data, bool end_stream) {
  ENVOY_BUG(response_state_ == FilterState::IDLE, "Filter response state should be idle");
  openStream();
  response_body_chunk_ = &data;
  ProcessingRequest req;
  auto* body_req = req.mutable_response_body();
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  response_state_ = FilterState::BODY;
  ENVOY_LOG(debug, "Sending response_body message. end_stream = {}", end_stream);
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterDataStatus::StopIterationAndWatermark;
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  auto response = std::move(r);
  bool message_handled = false;
  ENVOY_LOG(debug, "Received gRPC message. State = {}", request_state_);

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
    // Ignore messages received out of order. However, ignore the stream
    // protect ourselves since the server is not following the protocol.
    ENVOY_LOG(warn, "Spurious response message received on gRPC stream");
    cleanupState();
    processing_complete_ = true;
  }
}

bool Filter::handleRequestHeadersResponse(const HeadersResponse& response) {
  if (request_state_ == FilterState::HEADERS) {
    ENVOY_LOG(debug, "applying request_headers response");
    MutationUtils::applyCommonHeaderResponse(response, *request_headers_);
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
    return true;
  }
  return false;
}

bool Filter::handleResponseHeadersResponse(const HeadersResponse& response) {
  if (response_state_ == FilterState::HEADERS) {
    ENVOY_LOG(debug, "applying response_headers response");
    MutationUtils::applyCommonHeaderResponse(response, *response_headers_);
    response_state_ = FilterState::IDLE;
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

bool Filter::handleRequestBodyResponse(const BodyResponse& response) {
  if (request_state_ == FilterState::BODY) {
    ENVOY_LOG(debug, "Applying request_body response");
    MutationUtils::applyCommonBodyResponse(response, *request_body_chunk_);
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
    return true;
  }
  return false;
}

bool Filter::handleResponseBodyResponse(const BodyResponse& response) {
  if (response_state_ == FilterState::BODY) {
    ENVOY_LOG(debug, "Applying response_body response");
    MutationUtils::applyCommonBodyResponse(response, *response_body_chunk_);
    response_state_ = FilterState::IDLE;
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

void Filter::handleImmediateResponse(const ImmediateResponse& response) {
  // We don't want to process any more stream messages after this.
  // Close the stream before sending because "sendLocalResponse" may trigger
  // additional calls to this filter.
  request_state_ = FilterState::IDLE;
  response_state_ = FilterState::IDLE;
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
  if (request_state_ != FilterState::IDLE) {
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
  }
  if (response_state_ != FilterState::IDLE) {
    response_state_ = FilterState::IDLE;
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