#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/sidestream_watermark.h"
#include "source/extensions/filters/http/ext_proc/client_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExternalProcessorStream : public StreamBase {
public:
  virtual ~ExternalProcessorStream() = default;
  virtual void send(envoy::service::ext_proc::v3::ProcessingRequest&& request,
                    bool end_stream) PURE;
  // Idempotent close. Return true if it actually closed.
  virtual bool close() PURE;
  virtual const StreamInfo::StreamInfo& streamInfo() const PURE;
  virtual StreamInfo::StreamInfo& streamInfo() PURE;
  virtual void notifyFilterDestroy() PURE;
};

using ExternalProcessorStreamPtr = std::unique_ptr<ExternalProcessorStream>;

class ExternalProcessorCallbacks : public RequestCallbacks {
public:
  virtual ~ExternalProcessorCallbacks() = default;
  virtual void onReceiveMessage(
      std::unique_ptr<envoy::service::ext_proc::v3::ProcessingResponse>&& response) PURE;
  virtual void onGrpcError(Grpc::Status::GrpcStatus error) PURE;
  virtual void onGrpcClose() PURE;
  virtual void logGrpcStreamInfo() PURE;
};

class ExternalProcessorClient : public ClientBase {
public:
  virtual ~ExternalProcessorClient() = default;
  virtual ExternalProcessorStreamPtr
  start(ExternalProcessorCallbacks& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        const Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) PURE;
};

using ExternalProcessorClientPtr = std::unique_ptr<ExternalProcessorClient>;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
