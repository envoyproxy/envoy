#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/sidestream_watermark.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/common/ext_proc/grpc_client.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExternalProcessing {

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
         ProcessorCallbacks<ResponseType>& callbacks, Http::AsyncClient::StreamOptions& options,
         Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks,
         absl::string_view service_method);

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
  ProcessorStreamImpl(ProcessorCallbacks<ResponseType>& callbacks, absl::string_view service_method)
      : callbacks_(callbacks), service_method_(service_method),
        grpc_side_stream_flow_control_(Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.grpc_side_stream_flow_control")) {}

  // Start the gRPC async stream: It returns true if the start succeeded. Otherwise it returns false
  // if it failed to start.
  bool startStream(Grpc::AsyncClient<RequestType, ResponseType>&& client,
                   const Http::AsyncClient::StreamOptions& options);
  // Optional reference to filter object.
  OptRef<ProcessorCallbacks<ResponseType>> callbacks_;
  Grpc::AsyncClient<RequestType, ResponseType> client_;
  Grpc::AsyncStream<RequestType> stream_;
  bool stream_closed_ = false;
  // The service method name. The service-method for ext-proc is statically
  // defined in Envoy's source files, so keeping a reference here is valid.
  absl::string_view service_method_;
  // Boolean flag initiated by runtime flag.
  const bool grpc_side_stream_flow_control_;
};

// Implementation of all template methods below
template <typename RequestType, typename ResponseType>
ProcessorStreamPtr<RequestType, ResponseType>
ProcessorStreamImpl<RequestType, ResponseType>::create(
    Grpc::AsyncClient<RequestType, ResponseType>&& client,
    ProcessorCallbacks<ResponseType>& callbacks, Http::AsyncClient::StreamOptions& options,
    Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks,
    absl::string_view service_method) {
  auto stream = std::unique_ptr<ProcessorStreamImpl<RequestType, ResponseType>>(
      new ProcessorStreamImpl<RequestType, ResponseType>(callbacks, service_method));

  if (stream->grpcSidestreamFlowControl()) {
    options.setSidestreamWatermarkCallbacks(&sidestream_watermark_callbacks);
  }

  if (stream->startStream(std::move(client), options)) {
    return stream;
  }
  // Return nullptr on the start failure.
  return nullptr;
}

template <typename RequestType, typename ResponseType>
bool ProcessorStreamImpl<RequestType, ResponseType>::startStream(
    Grpc::AsyncClient<RequestType, ResponseType>&& client,
    const Http::AsyncClient::StreamOptions& options) {
  client_ = std::move(client);
  auto descriptor = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(service_method_);
  stream_ = client_.start(*descriptor, *this, options);
  // Returns true if the start succeeded and returns false on start failure.
  return stream_ != nullptr;
}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::send(RequestType&& request, bool end_stream) {
  stream_.sendMessage(std::move(request), end_stream);
}

template <typename RequestType, typename ResponseType>
bool ProcessorStreamImpl<RequestType, ResponseType>::close() {
  if (!stream_closed_) {
    ENVOY_LOG(debug, "Closing gRPC stream");
    // Unregister the watermark callbacks, if any exist (e.g., filter is not destroyed yet)
    if (grpc_side_stream_flow_control_ && callbacks_.has_value()) {
      stream_.removeWatermarkCallbacks();
    }
    stream_.closeStream();
    stream_closed_ = true;
    stream_.resetStream();
    return true;
  }
  return false;
}

template <typename RequestType, typename ResponseType>
bool ProcessorStreamImpl<RequestType, ResponseType>::halfCloseAndDeleteOnRemoteClose() {
  if (!stream_closed_) {
    ENVOY_LOG(debug, "Closing gRPC stream");
    // Unregister the watermark callbacks, if any exist (e.g., filter is not destroyed yet)
    if (grpc_side_stream_flow_control_ && callbacks_.has_value()) {
      stream_.removeWatermarkCallbacks();
    }
    // half close client side of the stream
    stream_.closeStream();
    stream_closed_ = true;
    // schedule deletion when server half-closes or remote close timer expires
    stream_.waitForRemoteCloseAndDelete();
    return true;
  }
  return false;
}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onReceiveMessage(
    std::unique_ptr<ResponseType>&& response) {
  if (!callbacks_.has_value()) {
    ENVOY_LOG(debug, "Underlying filter object has been destroyed.");
    return;
  }
  callbacks_->onReceiveMessage(std::move(response));
}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onCreateInitialMetadata(
    Http::RequestHeaderMap&) {}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onReceiveInitialMetadata(
    Http::ResponseHeaderMapPtr&&) {}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onReceiveTrailingMetadata(
    Http::ResponseTrailerMapPtr&&) {}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onRemoteClose(Grpc::Status::GrpcStatus status,
                                                                   const std::string& message) {
  ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status, message);
  stream_closed_ = true;

  if (!callbacks_.has_value()) {
    ENVOY_LOG(debug, "Underlying filter object has been destroyed.");
    return;
  }

  callbacks_->logStreamInfo();
  if (status == Grpc::Status::Ok) {
    callbacks_->onGrpcClose();
  } else {
    callbacks_->onGrpcError(status, message);
  }
}

template <typename RequestType, typename ResponseType>
using ProcessorClientPtr = std::unique_ptr<ProcessorClient<RequestType, ResponseType>>;

/**
 * Implementation of ProcessorClient that uses a gRPC service.
 */
template <typename RequestType, typename ResponseType>
class ProcessorClientImpl : public ProcessorClient<RequestType, ResponseType> {
public:
  ProcessorClientImpl(Grpc::AsyncClientManager& client_manager, Stats::Scope& scope,
                      absl::string_view service_method)
      : client_manager_(client_manager), scope_(scope), service_method_(service_method) {}

  ProcessorStreamPtr<RequestType, ResponseType>
  start(ProcessorCallbacks<ResponseType>& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) override {
    auto client_or_error =
        client_manager_.getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, scope_, true);
    if (!client_or_error.status().ok()) {
      ENVOY_LOG_PERIODIC_MISC(error, std::chrono::seconds(10), "Creating raw asyc client failed {}",
                              client_or_error.status());
      return nullptr;
    }
    Grpc::AsyncClient<RequestType, ResponseType> grpcClient(client_or_error.value());
    return ProcessorStreamImpl<RequestType, ResponseType>::create(
        std::move(grpcClient), callbacks, options, sidestream_watermark_callbacks, service_method_);
  }

  void sendRequest(RequestType&& request, bool end_stream, const uint64_t,
                   RequestCallbacks<ResponseType>*, StreamBase* stream) override {
    auto* grpc_stream = dynamic_cast<ProcessorStream<RequestType, ResponseType>*>(stream);
    if (grpc_stream != nullptr) {
      grpc_stream->send(std::move(request), end_stream);
    }
  };

  void cancel() override {}
  const Envoy::StreamInfo::StreamInfo* getStreamInfo() const override { return nullptr; }

private:
  Grpc::AsyncClientManager& client_manager_;
  Stats::Scope& scope_;
  // The service-method for ext-proc is statically defined in Envoy's source
  // files, so keeping a reference here is valid.
  absl::string_view service_method_;
};

} // namespace ExternalProcessing
} // namespace Common
} // namespace Extensions
} // namespace Envoy
