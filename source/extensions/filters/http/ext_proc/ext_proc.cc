#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "extensions/filters/http/ext_proc/headers.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterHeadersStatus;
using Http::RequestHeaderMap;

static const std::string kErrorPrefix = "ext_proc error";

void Filter::onDestroy() {
  if (stream_ && !stream_closed_) {
    ENVOY_LOG(debug, "Closing gRPC stream to processing server");
    stream_->close();
  }
}

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_of_stream) {
  // We're at the start, so start the stream and send a headers message
  request_headers_ = &headers;
  stream_ = client_->start(*this, config_->grpcTimeout());
  ProcessingRequest req;
  auto headers_req = req.mutable_request_headers();
  buildHttpHeaders(headers, headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_of_stream);
  request_state_ = FilterState::HEADERS;
  stream_->send(std::move(req), false);

  // Wait until we have a gRPC response before allowing any more callbacks
  return FilterHeadersStatus::StopAllIterationAndWatermark;
}

void Filter::onReceiveMessage(
    std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& r) {
  auto response = std::move(r);
  bool message_valid = false;
  ENVOY_LOG(debug, "Received gRPC message. State = {}", request_state_);

  // This next section will grow as we support the rest of the protocol
  if (request_state_ == FilterState::HEADERS) {
    if (response->has_request_headers()) {
      ENVOY_LOG(debug, "applying request_headers response");
      message_valid = true;
      const auto headers_response = response->request_headers();
      if (headers_response.has_response()) {
        const auto& common_response = headers_response.response();
        if (common_response.has_header_mutation()) {
          applyHeaderMutations(common_response.header_mutation(), request_headers_);
        }
      }
    }
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
  }

  if (!message_valid) {
    // Ignore messages received out of order. However, close the stream to
    // protect ourselves since the server is not following the protocol.
    ENVOY_LOG(warn, "Spurious response message received on gRPC stream");
    stream_closed_ = true;
    stream_->close();
  }
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stream_closed_ = true;
  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
  } else {
    // Use a switch here now because there will be more than two
    // cases very soon.
    switch (request_state_) {
    case HEADERS:
      request_state_ = FilterState::IDLE;
      decoder_callbacks_->sendLocalReply(
          Http::Code::InternalServerError, "", nullptr, absl::nullopt,
          absl::StrFormat("%s: gRPC error %i", kErrorPrefix, status));
      break;
    default:
      // Nothing else to do
      break;
    }
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");
  stream_closed_ = true;
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  // Use a switch here now because there will be more than two
  // cases very soon.
  switch (request_state_) {
  case HEADERS:
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
    break;
  default:
    // Nothing to do otherwise
    break;
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy