#pragma once

#include "envoy/server/factory_context.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"
#include "source/extnsions/http/ext_proc/save_processing_response/save_processing_response.h"

namespace Http {
namespace ExternalProcessing {

class SaveProcessingResponseFactory : public OnProcessingResponseFactory {
public:
  ~SaveProcessingResponseFactory() override = default;
  std::unique_ptr<OnProcessingResponse> createOnProcessingResponse(
      const Protobuf::Message&,
      const Envoy::Server::Configuration::CommonFactoryContext&) const override {
    return std::make_unique<SaveProcessingResponse>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override;
};

} // namespace ExternalProcessing
} // namespace Http
