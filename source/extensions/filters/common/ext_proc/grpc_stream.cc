#include "source/extensions/filters/common/ext_proc/grpc_stream.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExtProc {

template <typename RequestType, typename ResponseType>
ProcessorStreamPtr<RequestType, ResponseType> ProcessorStreamImpl<RequestType, ResponseType>::create(
    Grpc::AsyncClient<RequestType, ResponseType>&& client,
    ProcessorCallbacks<RequestType, ResponseType>& callbacks, 
    Http::AsyncClient::StreamOptions& options,
    Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks,
    const std::string& service_method) {
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
  grpc_context_ = options.parent_context;
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
void ProcessorStreamImpl<RequestType, ResponseType>::onCreateInitialMetadata(Http::RequestHeaderMap&) {}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {}

template <typename RequestType, typename ResponseType>
void ProcessorStreamImpl<RequestType, ResponseType>::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) {}

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

} // namespace ExtProc
} // namespace Common
} // namespace Extensions
} // namespace Envoy
