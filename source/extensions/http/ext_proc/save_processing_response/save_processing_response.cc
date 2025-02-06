#include "source/extensions/http/ext_proc/save_processing_response/save_processing_response.h"
#include "envoy/stream_info/stream_info.h"

namespace Http {
namespace ExternalProcessing {
void SaveProcessingResponse::afterProcessingRequestHeaders(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_request_headers());
  ASSERT(response.request_headers().has_response() &&
         response.request_headers().response().has_header_mutation());
  stream_info.setDynamicMetadata(
      "envoy.test.ext_proc.request_headers_response",
      getHeaderMutations(response.request_headers().response().header_mutation()));
}

void SaveProcessingResponse::afterProcessingResponseHeaders(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_response_headers());
  ASSERT(response.response_headers().has_response() &&
         response.response_headers().response().has_header_mutation());
  stream_info.setDynamicMetadata(
      "envoy.test.ext_proc.response_headers_response",
      getHeaderMutations(response.response_headers().response().header_mutation()));
}

void SaveProcessingResponse::afterProcessingRequestBody(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_request_body());
  ASSERT(response.request_body().has_response() &&
         response.request_body().response().has_body_mutation());
  stream_info.setDynamicMetadata(
      "envoy.test.ext_proc.request_body_response",
      getBodyMutation(response.request_body().response().body_mutation()));
}

void SaveProcessingResponse::afterProcessingResponseBody(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_response_body());
  ASSERT(response.response_body().has_response() &&
         response.response_body().response().has_body_mutation());
  stream_info.setDynamicMetadata(
      "envoy.test.ext_proc.response_body_response",
      getBodyMutation(response.response_body().response().body_mutation()));
}

void SaveProcessingResponse::afterProcessingRequestTrailers(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_request_trailers());
  ASSERT(response.request_trailers().has_header_mutation());
  stream_info.setDynamicMetadata("envoy.test.ext_proc.request_trailers_response",
                                 getHeaderMutations(response.request_trailers().header_mutation()));
}

void SaveProcessingResponse::afterProcessingResponseTrailers(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  ASSERT(response.has_response_trailers());
  ASSERT(response.response_trailers().has_header_mutation());
  stream_info.setDynamicMetadata(
      "envoy.test.ext_proc.response_trailers_response",
      getHeaderMutations(response.response_trailers().header_mutation()));
}

} // namespace ExternalProcessing
} // namespace Http
