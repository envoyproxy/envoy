#include "source/extensions/filters/http/ext_proc/client_impl.h"

#include "source/extensions/filters/common/ext_proc/grpc_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

ExternalProcessorClientPtr createExternalProcessorClient(Grpc::AsyncClientManager& client_manager,
                                                         Stats::Scope& scope) {
  static constexpr char kExternalMethod[] = "envoy.service.ext_proc.v3.ExternalProcessor.Process";
  return std::make_unique<
      CommonExtProc::ProcessorClientImpl<ProcessingRequest, ProcessingResponse>>(
      client_manager, scope, kExternalMethod);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
