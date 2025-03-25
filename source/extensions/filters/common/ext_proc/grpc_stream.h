#pragma once

#include <memory>

#include "envoy/grpc/async_client.h"
#include "envoy/http/async_client.h"

#include "source/extensions/filters/common/ext_proc/grpc_client.h"
#include "source/common/http/sidestream_watermark.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExtProc {

/**
 * Implementation of ProcessorStream that uses a gRPC service.
 */
template <typename RequestType, typename ResponseType>
class ProcessorStreamImpl : public ProcessorStream<RequestType, ResponseType>,
                            public Grpc::AsyncStreamCallbacks<ResponseType>,
                            public Logger::Loggable<Logger::Id::ext_proc> {
public:
  // Factory method: create and return `ProcessorStreamPtr`; return nullptr on failure.
  static ProcessorStreamPtr<RequestType, ResponseType>
  create(Grpc::AsyncClient<RequestType, ResponseType>&& client,
         ProcessorCallbacks<RequestType, ResponseType>& callbacks,
         Http::AsyncClient::StreamOptions& options,
         Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks,
         const std::string& service_method);

  void send(RequestType&& request, bool end_stream) override;
  // Close the stream. This is idempotent and will return true if we
  // actually closed it.
  bool close() override;
  bool halfCloseAndDeleteOnRemoteClose() override;

  void notifyFilterDestroy() override {
    // When the filter object is being destroyed, `callbacks_` (which is a OptRef to filter object)
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
  void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override;

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
  ProcessorStreamImpl(ProcessorCallbacks<RequestType, ResponseType>& callbacks,
                     const std::string& service_method)
      : callbacks_(callbacks), service_method_(service_method),
        grpc_side_stream_flow_control_(Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.grpc_side_stream_flow_control")) {}

  // Start the gRPC async stream: It returns true if the start succeeded. Otherwise it returns false
  // if it failed to start.
  bool startStream(Grpc::AsyncClient<RequestType, ResponseType>&& client,
                   const Http::AsyncClient::StreamOptions& options);
  // Optional reference to filter object.
  OptRef<ProcessorCallbacks<RequestType, ResponseType>> callbacks_;
  Grpc::AsyncClient<RequestType, ResponseType> client_;
  Grpc::AsyncStream<RequestType> stream_;
  Http::AsyncClient::ParentContext grpc_context_;
  bool stream_closed_ = false;
  // Service method name
  const std::string service_method_;
  // Boolean flag initiated by runtime flag.
  const bool grpc_side_stream_flow_control_;
};

} // namespace ExtProc
} // namespace Common
} // namespace Extensions
} // namespace Envoy
