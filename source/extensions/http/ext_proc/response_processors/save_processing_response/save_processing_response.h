#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.h"
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
      "envoy.http.ext_proc.response_processors.save_processing_response";
  struct Response {
    absl::Status processing_status;
    envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  };
  absl::optional<Response> response;
};

class SaveProcessingResponse
    : public Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponse {
public:
  using SaveProcessingResponseProto = envoy::extensions::http::ext_proc::response_processors::
      save_processing_response::v3::SaveProcessingResponse;

  SaveProcessingResponse(const SaveProcessingResponseProto&);

  void
  afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  // Not implemented.
  void afterProcessingRequestBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                  absl::Status, Envoy::StreamInfo::StreamInfo&) override{};
  // Not implemented.
  void afterProcessingResponseBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                   absl::Status, Envoy::StreamInfo::StreamInfo&) override{};
  void afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterReceivingImmediateResponse(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;

private:
  struct SaveOptions {
    SaveOptions(const SaveProcessingResponseProto::SaveOptions& save_options)
        : save_response{save_options.save_response()}, save_on_error{save_options.save_on_error()} {
    }
    const bool save_response = false;
    const bool save_on_error = false;
  };

  void addToFilterState(const SaveOptions& save_options,
                        const envoy::service::ext_proc::v3::ProcessingResponse& processing_response,
                        absl::Status status, Envoy::StreamInfo::StreamInfo& stream_info);

  const std::string filter_state_name_;

  SaveOptions save_request_headers_;
  SaveOptions save_response_headers_;
  SaveOptions save_request_trailers_;
  SaveOptions save_response_trailers_;
  SaveOptions save_immediate_response_;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
