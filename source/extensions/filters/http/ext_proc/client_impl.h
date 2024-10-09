#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/ext_proc/client.h"

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ProcessingResponsePtr = std::unique_ptr<ProcessingResponse>;

class ExternalProcessorClientImpl : public ExternalProcessorClient {
public:
  ExternalProcessorClientImpl(Grpc::AsyncClientManager& client_manager, Stats::Scope& scope);

  ExternalProcessorStreamPtr
  start(ExternalProcessorCallbacks& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        const Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) override;
  void sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& request, bool end_stream,
                   const uint64_t stream_id, RequestCallbacks* callbacks,
                   StreamBase* stream) override;
  void cancel() override {}

private:
  Grpc::AsyncClientManager& client_manager_;
  Stats::Scope& scope_;
};

class ExternalProcessorStreamImpl : public ExternalProcessorStream,
                                    public Grpc::AsyncStreamCallbacks<ProcessingResponse>,
                                    public Logger::Loggable<Logger::Id::ext_proc> {
public:
  // Factory method: create and return `ExternalProcessorStreamPtr`; return nullptr on failure.
  static ExternalProcessorStreamPtr
  create(Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
         ExternalProcessorCallbacks& callbacks, const Http::AsyncClient::StreamOptions& options,
         Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks);

  void send(ProcessingRequest&& request, bool end_stream) override;
  // Close the stream. This is idempotent and will return true if we
  // actually closed it.
  bool close() override;

  void notifyFilterDestroy() override {
    // When the filter object is being destroyed,  `callbacks_` (which is a OptRef to filter object)
    // should be reset to avoid the dangling reference.
    callbacks_.reset();

    // Unregister the watermark callbacks(if any) to prevent access of filter callbacks after
    // the filter object is destroyed.
    if (!stream_closed_) {
      // Remove the parent stream info to avoid a dangling reference.
      stream_.streamInfo().clearParentStreamInfo();
      if (grpc_side_stream_flow_control_) {
        stream_.removeWatermarkCallbacks();
      }
    }
  }

  // AsyncStreamCallbacks
  void onReceiveMessage(ProcessingResponsePtr&& message) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_.streamInfo(); }
  StreamInfo::StreamInfo& streamInfo() override { return stream_.streamInfo(); }

  bool grpcSidestreamFlowControl() { return grpc_side_stream_flow_control_; }

private:
  // Private constructor only can be invoked within this class.
  ExternalProcessorStreamImpl(ExternalProcessorCallbacks& callbacks)
      : callbacks_(callbacks), grpc_side_stream_flow_control_(Runtime::runtimeFeatureEnabled(
                                   "envoy.reloadable_features.grpc_side_stream_flow_control")) {}

  // Start the gRPC async stream: It returns true if the start succeeded. Otherwise it returns false
  // if it failed to start.
  bool startStream(Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
                   const Http::AsyncClient::StreamOptions& options);
  // Optional reference to filter object.
  OptRef<ExternalProcessorCallbacks> callbacks_;
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> client_;
  Grpc::AsyncStream<ProcessingRequest> stream_;
  Http::AsyncClient::ParentContext grpc_context_;
  bool stream_closed_ = false;
  // Boolean flag initiated by runtime flag.
  const bool grpc_side_stream_flow_control_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
