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

class OnProcessingResponse {
public:
  virtual ~OnProcessingResponse() = default;
  virtual void
  afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                 absl::Status processing_status,
                                 Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterProcessingRequestBody(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                             absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterProcessingResponseBody(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                              absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                 absl::Status processing_status,
                                 Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                  absl::Status processing_status,
                                  Envoy::StreamInfo::StreamInfo&) PURE;
  virtual void
  afterReceivingImmediateResponse(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                  absl::Status processing_status,
                                  Envoy::StreamInfo::StreamInfo&) PURE;
};

class OnProcessingResponseFactory : public Config::TypedFactory {
public:
  ~OnProcessingResponseFactory() override = default;
  virtual std::unique_ptr<OnProcessingResponse>
  createOnProcessingResponse(const Protobuf::Message& config,
                             Envoy::Server::Configuration::CommonFactoryContext& context,
                             const std::string& stats_prefix) const PURE;

  std::string category() const override { return "envoy.http.ext_proc.on_processing_response"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
