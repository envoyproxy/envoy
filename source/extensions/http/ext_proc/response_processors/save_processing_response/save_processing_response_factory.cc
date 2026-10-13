#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response_factory.h"

#include <string>

#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

std::string SaveProcessingResponseFactory::name() const {
  return "envoy.extensions.http.ext_proc.save_processing_response";
}

std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponse>
SaveProcessingResponseFactory::createOnProcessingResponse(
    const Protobuf::Message& config, Envoy::Server::Configuration::CommonFactoryContext& context,
    const std::string&) const {
  const SaveProcessingResponseProto& save_processing_response_config =
      MessageUtil::downcastAndValidate<const SaveProcessingResponseProto&>(
          config, context.messageValidationVisitor());
  return std::make_unique<SaveProcessingResponse>(save_processing_response_config);
}

REGISTER_FACTORY(SaveProcessingResponseFactory,
                 Envoy::Extensions::HttpFilters::ExternalProcessing::OnProcessingResponseFactory);

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
