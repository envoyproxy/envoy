#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "extensions/filters/http/ext_proc/mutation_utils.h"

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
      const auto& headers_response = response->request_headers();
      if (headers_response.has_response()) {
        const auto& common_response = headers_response.response();
        if (common_response.has_header_mutation()) {
          MutationUtils::applyHeaderMutations(common_response.header_mutation(), *request_headers_);
        }
      }
    } else if (response->has_immediate_response()) {
      // To be implemented later. Leave stream open to allow people to implement
      // correct servers that don't break us.
      message_valid = true;
    }
    request_state_ = FilterState::IDLE;
    decoder_callbacks_->continueDecoding();
  }

  if (message_valid) {
    stats_.stream_msgs_received_.inc();
  } else {
    stats_.spurious_msgs_received_.inc();
    // Ignore messages received out of order. However, close the stream to
    // protect ourselves since the server is not following the protocol.
    ENVOY_LOG(warn, "Spurious response message received on gRPC stream");
    closeStream();
  }
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stream_closed_ = true;
  stats_.streams_failed_.inc();
  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();
  } else {
    // Use a switch here now because there will be more than two
    // cases very soon.
    switch (request_state_) {
    case FilterState::HEADERS:
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
  stats_.streams_closed_.inc();
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  // Use a switch here now because there will be more than two
  // cases very soon.
  switch (request_state_) {
  case FilterState::HEADERS:
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