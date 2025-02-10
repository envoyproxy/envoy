#include "source/extensions/http/ext_proc/save_processing_response/save_processing_response.h"
#include "envoy/extensions/http/ext_proc/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

SaveProcessingResponse::SaveProcessingResponse(const SaveProcessingResponseProto& config)
    : save_request_headers_(config.save_request_headers()),
      save_response_headers_(config.save_response_headers()),
      save_request_body_(config.save_request_body()),
      save_response_body_(config.save_response_body()),
      save_request_trailers_(config.save_request_trailers()),
      save_response_trailers_(config.save_response_trailers()),
      save_immediate_response_(config.save_immediate_response()),
      save_on_error_(config.save_on_error()) {}

void SaveProcessingResponse::addToFilterState(
    const envoy::service::ext_proc::v3::ProcessingResponse& processing_response,
    absl::Status status, Envoy::StreamInfo::StreamInfo& stream_info) {
  if (status.ok() || save_on_error_) {
    SaveProcessingResponseFilterState* filter_state =
        stream_info.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
            SaveProcessingResponseFilterState::kFilterStateName);
    if (filter_state == nullptr) {
      auto shared_filter_state = std::make_shared<SaveProcessingResponseFilterState>();
      filter_state = shared_filter_state.get();
      stream_info.filterState()->setData(SaveProcessingResponseFilterState::kFilterStateName,
                                         shared_filter_state,
                                         Envoy::StreamInfo::FilterState::StateType::Mutable);
    }

    filter_state->responses.emplace_back(SaveProcessingResponseFilterState::Response{
        .processing_status = status, .processing_response = processing_response});
  }
}

void SaveProcessingResponse::afterProcessingRequestHeaders(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_request_headers_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingResponseHeaders(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_response_headers_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingRequestBody(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_request_body_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingResponseBody(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_response_body_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingRequestTrailers(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_request_trailers_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingResponseTrailers(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_response_trailers_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

void SaveProcessingResponse::afterReceivingImmediateResponse(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!save_immediate_response_) {
    return;
  }
  addToFilterState(response, status, stream_info);
}

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
