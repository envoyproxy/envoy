#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/http/ext_proc/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ext_proc/on_processing_response.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

struct SaveProcessingResponseFilterState
    : public std::enable_shared_from_this<SaveProcessingResponseFilterState>,
      public Envoy::StreamInfo::FilterState::Object {
  static constexpr absl::string_view kFilterStateName =
      "envoy.http.ext_proc.save_processing_response.filter_state";
  struct Response {
    absl::Status processing_status;
    envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  };
  std::vector<Response> responses;
};

class SaveProcessingResponse
    : public Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponse {
public:
  using SaveProcessingResponseProto =
      envoy::extensions::http::ext_proc::save_processing_response::v3::SaveProcessingResponse;

  SaveProcessingResponse(const SaveProcessingResponseProto&);

  void
  afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                  absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                   absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterReceivingImmediateResponse(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;

private:
  void addToFilterState(const envoy::service::ext_proc::v3::ProcessingResponse& processing_response,
                        absl::Status status, Envoy::StreamInfo::StreamInfo& stream_info);

  bool save_request_headers_ = false;
  bool save_response_headers_ = false;
  bool save_request_body_ = false;
  bool save_response_body_ = false;
  bool save_request_trailers_ = false;
  bool save_response_trailers_ = false;
  bool save_immediate_response_ = false;
  bool save_on_error_ = false;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
