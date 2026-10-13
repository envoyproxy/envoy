#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Interface to inject custom decorators that are called after receiving a response from the
// external processor.
class OnProcessingResponse {
public:
  virtual ~OnProcessingResponse() = default;

  // Called after processing the response from the external processor with :ref:`request_headers
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.request_headers>` set.
  virtual void
  afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::HeadersResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`response_headers
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.response_headers>` set.
  virtual void
  afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::HeadersResponse& response,
                                 absl::Status processing_status,
                                 Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`request_body
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.request_body>` set.
  virtual void
  afterProcessingRequestBody(const envoy::service::ext_proc::v3::BodyResponse& response,
                             absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`response_body
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.response_body>` set.
  virtual void
  afterProcessingResponseBody(const envoy::service::ext_proc::v3::BodyResponse& response,
                              absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`request_trailers
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.request_trailers>` set.
  virtual void
  afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::TrailersResponse& response,
                                 absl::Status processing_status,
                                 Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`response_trailers
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.response_trailers>` set.
  virtual void
  afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::TrailersResponse& response,
                                  absl::Status processing_status,
                                  Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with :ref:`immediate_response
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.immediate_response>` set.
  virtual void
  afterReceivingImmediateResponse(const envoy::service::ext_proc::v3::ImmediateResponse& response,
                                  absl::Status processing_status,
                                  Envoy::StreamInfo::StreamInfo&) PURE;

  // Called after processing the response from the external processor with
  // :ref:`streamed_immediate_response
  // <envoy_v3_api_field_service.ext_proc.v3.ProcessingResponse.streamed_immediate_response>` set.
  virtual void afterProcessingStreamingImmediateResponse(
      const envoy::service::ext_proc::v3::StreamedImmediateResponse& response,
      absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) PURE;
};

class OnProcessingResponseFactory : public Config::TypedFactory {
public:
  ~OnProcessingResponseFactory() override = default;
  virtual std::unique_ptr<OnProcessingResponse>
  createOnProcessingResponse(const Protobuf::Message& config,
                             Envoy::Server::Configuration::CommonFactoryContext& context,
                             const std::string& stats_prefix) const PURE;

  std::string category() const override { return "envoy.http.ext_proc.response_processors"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
