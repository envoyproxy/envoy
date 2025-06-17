#include "source/extensions/filters/network/ext_proc/client_impl.h"

#include "source/extensions/filters/common/ext_proc/grpc_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

ExternalProcessorClientPtr createExternalProcessorClient(Grpc::AsyncClientManager& client_manager,
                                                         Stats::Scope& scope) {
  static constexpr absl::string_view kExternalMethod =
      "envoy.service.network_ext_proc.v3.NetworkExternalProcessor.Process";
  return std::make_unique<
      CommonExtProc::ProcessorClientImpl<ProcessingRequest, ProcessingResponse>>(
      client_manager, scope, kExternalMethod);
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
