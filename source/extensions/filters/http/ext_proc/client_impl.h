#pragma once

#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/sidestream_watermark.h"
#include "source/extensions/filters/common/ext_proc/grpc_client.h"
#include "source/extensions/filters/common/ext_proc/grpc_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

namespace CommonExtProc = Envoy::Extensions::Common::ExternalProcessing;

using ProcessingRequest = envoy::service::ext_proc::v3::ProcessingRequest;
using ProcessingResponse = envoy::service::ext_proc::v3::ProcessingResponse;

using ExternalProcessorCallbacks = CommonExtProc::ProcessorCallbacks<ProcessingResponse>;

using ExternalProcessorStream =
    CommonExtProc::ProcessorStream<ProcessingRequest, ProcessingResponse>;
using ExternalProcessorStreamPtr = std::unique_ptr<ExternalProcessorStream>;

using ExternalProcessorClient =
    CommonExtProc::ProcessorClient<ProcessingRequest, ProcessingResponse>;
using ExternalProcessorClientPtr = std::unique_ptr<ExternalProcessorClient>;

using ClientBasePtr = CommonExtProc::ClientBasePtr<ProcessingRequest, ProcessingResponse>;

inline ExternalProcessorClientPtr
createExternalProcessorClient(Grpc::AsyncClientManager& client_manager, Stats::Scope& scope) {
  static constexpr char kExternalMethod[] = "envoy.service.ext_proc.v3.ExternalProcessor.Process";
  return std::make_unique<
      CommonExtProc::ProcessorClientImpl<ProcessingRequest, ProcessingResponse>>(
      client_manager, scope, kExternalMethod);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
