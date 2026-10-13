#pragma once

#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/network_ext_proc/v3/network_external_processor.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/sidestream_watermark.h"
#include "source/extensions/filters/common/ext_proc/grpc_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

namespace CommonExtProc = Envoy::Extensions::Common::ExternalProcessing;

using ProcessingRequest = envoy::service::network_ext_proc::v3::ProcessingRequest;
using ProcessingResponse = envoy::service::network_ext_proc::v3::ProcessingResponse;

using ExternalProcessorCallbacks = CommonExtProc::ProcessorCallbacks<ProcessingResponse>;

using ExternalProcessorStream =
    CommonExtProc::ProcessorStream<ProcessingRequest, ProcessingResponse>;
using ExternalProcessorStreamPtr = std::unique_ptr<ExternalProcessorStream>;

using ExternalProcessorClient =
    CommonExtProc::ProcessorClient<ProcessingRequest, ProcessingResponse>;
using ExternalProcessorClientPtr = std::unique_ptr<ExternalProcessorClient>;

ExternalProcessorClientPtr createExternalProcessorClient(Grpc::AsyncClientManager& client_manager,
                                                         Stats::Scope& scope);

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
