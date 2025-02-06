#include "source/extensions/http/ext_proc/save_processing_response/save_processing_response_factory.h"

#include "envoy/registry/registry.h"

namespace Http {
namespace ExternalProcessing {

std::string SaveProcessingResponseFactory::name() const {
  return "envoy.extensions.http.ext_proc.save_processing_response";
}

REGISTER_FACTORY(SaveProcessingResponseFactory, OnProcessingResponseFactory);

} // namespace ExternalProcessing
} // namespace Http
