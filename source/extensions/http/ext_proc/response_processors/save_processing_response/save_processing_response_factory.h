#pragma once

#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.validate.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/ext_proc/on_processing_response.h"
#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

class SaveProcessingResponseFactory
    : public Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponseFactory {
public:
  using SaveProcessingResponseProto = envoy::extensions::http::ext_proc::response_processors::
      save_processing_response::v3::SaveProcessingResponse;
  ~SaveProcessingResponseFactory() override = default;

  std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponse>
  createOnProcessingResponse(const Protobuf::Message& config,
                             Envoy::Server::Configuration::CommonFactoryContext& context,
                             const std::string&) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::http::ext_proc::response_processors::
                                         save_processing_response::v3::SaveProcessingResponse()};
  }

  std::string name() const override;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
