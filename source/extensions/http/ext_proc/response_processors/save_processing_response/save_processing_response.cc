#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response.h"

#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

SaveProcessingResponse::SaveProcessingResponse(const SaveProcessingResponseProto& config)
    : filter_state_name_(config.filter_state_name_suffix().empty()
                             ? SaveProcessingResponseFilterState::kFilterStateName
                             : absl::StrCat(SaveProcessingResponseFilterState::kFilterStateName,
                                            ".", config.filter_state_name_suffix())),
      save_request_headers_(config.save_request_headers()),
      save_response_headers_(config.save_response_headers()),
      save_request_trailers_(config.save_request_trailers()),
      save_response_trailers_(config.save_response_trailers()),
      save_immediate_response_(config.save_immediate_response()) {}

void SaveProcessingResponse::addToFilterState(
    envoy::service::ext_proc::v3::ProcessingResponse processing_response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  SaveProcessingResponseFilterState* filter_state =
      stream_info.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
          filter_state_name_);
  if (filter_state == nullptr) {
    auto shared_filter_state = std::make_shared<SaveProcessingResponseFilterState>();
    filter_state = shared_filter_state.get();
    stream_info.filterState()->setData(filter_state_name_, shared_filter_state,
                                       Envoy::StreamInfo::FilterState::StateType::Mutable);
  }

  filter_state->response.emplace(SaveProcessingResponseFilterState::Response{
      .processing_status = status, .processing_response = processing_response});
}

void SaveProcessingResponse::afterProcessingRequestHeaders(
    const envoy::service::ext_proc::v3::HeadersResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_request_headers_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_request_headers() = response;
  addToFilterState(processing_response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingResponseHeaders(
    const envoy::service::ext_proc::v3::HeadersResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_response_headers_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_response_headers() = response;
  addToFilterState(processing_response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingRequestTrailers(
    const envoy::service::ext_proc::v3::TrailersResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_request_trailers_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_request_trailers() = response;
  addToFilterState(processing_response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingResponseTrailers(
    const envoy::service::ext_proc::v3::TrailersResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_response_trailers_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_response_trailers() = response;
  addToFilterState(processing_response, status, stream_info);
}

void SaveProcessingResponse::afterReceivingImmediateResponse(
    const envoy::service::ext_proc::v3::ImmediateResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_immediate_response_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_immediate_response() = response;
  addToFilterState(processing_response, status, stream_info);
}

void SaveProcessingResponse::afterProcessingStreamingImmediateResponse(
    const envoy::service::ext_proc::v3::StreamedImmediateResponse& response, absl::Status status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (!shouldSaveResponse(save_immediate_response_, status)) {
    return;
  }
  envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  *processing_response.mutable_streamed_immediate_response() = response;
  addToFilterState(processing_response, status, stream_info);
}

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
