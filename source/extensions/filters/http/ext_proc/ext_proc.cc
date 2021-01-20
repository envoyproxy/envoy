#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterHeadersStatus;
using Http::RequestHeaderMap;
using Http::ResponseHeaderMap;

static const std::string kErrorPrefix = "ext_proc error";

void Filter::closeStream() {
  if (!stream_closed_) {
    if (stream_) {
      ENVOY_LOG(debug, "Closing gRPC stream to processing server");
      stream_->close();
      stats_.streams_closed_.inc();
    }
    stream_closed_ = true;
  }
}

void Filter::onDestroy() { closeStream(); }

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_of_stream) {
  // We're at the start, so start the stream and send a headers message
  request_headers_ = &headers;
  stream_ = client_->start(*this, config_->grpcTimeout());
  stats_.streams_started_.inc();
  ProcessingRequest req;
  auto* headers_req = req.mutable_request_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_of_stream);
  request_state_ = FilterState::HEADERS;
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();

  // Wait until we have a gRPC response before allowing any more callbacks
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_of_stream) {
  if (pending_error_) {
    sendImmediateResponse(*pending_error_, false);
    pending_error_.reset();
    return FilterHeadersStatus::Continue;
  }
  if (stream_closed_) {
    return FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  ProcessingRequest req;
  auto* headers_req = req.mutable_response_headers();
  MutationUtils::buildHttpHeaders(headers, *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_of_stream);
  response_state_ = FilterState::HEADERS;
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

void Filter::onReceiveMessage(
    std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& r) {
  auto response = std::move(r);
  bool message_handled = false;
  ENVOY_LOG(debug, "Received gRPC message. State = {}", request_state_);

  if (response->has_request_headers()) {
    message_handled = handleRequestHeadersResponse(response->request_headers());
  } else if (response->has_response_headers()) {
    message_handled = handleResponseHeadersResponse(response->response_headers());
  } else if (response->has_immediate_response()) {
    message_handled = handleImmediateResponse(response->immediate_response());
  }

  if (message_handled) {
    stats_.stream_msgs_received_.inc();
  } else {
    stats_.spurious_msgs_received_.inc();
    // Ignore messages received out of order. However, close the stream to
    // protect ourselves since the server is not following the protocol.
    ENVOY_LOG(warn, "Spurious response message received on gRPC stream");
    cleanupState();
    closeStream();
  }
}

bool Filter::handleRequestHeadersResponse(const HeadersResponse& response) {
  if (request_state_ == FilterState::HEADERS) {
    ENVOY_LOG(debug, "applying request_headers response");
    if (response.has_response()) {
      const auto& common_response = response.response();
      if (common_response.has_header_mutation()) {
        MutationUtils::applyHeaderMutations(common_response.header_mutation(), *request_headers_);
      }
    }
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
    return true;
  }
  return false;
}

bool Filter::handleResponseHeadersResponse(const HeadersResponse& response) {
  if (response_state_ == FilterState::HEADERS) {
    ENVOY_LOG(debug, "applying response_headers response");
    if (response.has_response()) {
      const auto& common_response = response.response();
      if (common_response.has_header_mutation()) {
        MutationUtils::applyHeaderMutations(common_response.header_mutation(), *response_headers_);
      }
    }
    response_state_ = FilterState::IDLE;
    encoder_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

bool Filter::handleImmediateResponse(const ImmediateResponse& response) {
  if (response_state_ == FilterState::HEADERS) {
    // Waiting for a response headers response, so return immediately now.
    // Do this first in case both are in progress.
    // We don't want to process any more stream messages after this.
    // Close the stream before sending because "sendLocalResponse" triggers
    // additional calls to this filter.
    response_state_ = FilterState::IDLE;
    closeStream();

    ENVOY_LOG(debug, "Returning immediate response from processor on encoding path");
    sendImmediateResponse(response, false);

    return true;

  } else if (request_state_ == FilterState::HEADERS) {
    ENVOY_LOG(debug, "Returning immediate response from processor on decoding path");
    request_state_ = FilterState::IDLE;
    closeStream();
    sendImmediateResponse(response, true);
    return true;
  }

  return false;
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stats_.streams_failed_.inc();

  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();

  } else {
    stream_closed_ = true;
    auto error_response = std::make_unique<ImmediateResponse>();
    error_response->mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    error_response->set_details(absl::StrFormat("%s: gRPC error %i", kErrorPrefix, status));
    if (!handleImmediateResponse(*error_response)) {
      // This type of error needs to be delivered the next time that we get
      // a chance.
      pending_error_ = std::move(error_response);
    }
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");
  stream_closed_ = true;
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

void Filter::sendImmediateResponse(const ImmediateResponse& response, bool on_decoding) {
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

  if (on_decoding) {
    decoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                       mutate_headers, grpc_status, response.details());
  } else {
    encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                       mutate_headers, grpc_status, response.details());
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy