#include "source/extensions/filters/http/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

static constexpr char kExternalMethod[] = "envoy.service.ext_proc.v3.ExternalProcessor.Process";

ExternalProcessorClientImpl::ExternalProcessorClientImpl(Grpc::AsyncClientManager& client_manager,
                                                         Stats::Scope& scope)
    : client_manager_(client_manager), scope_(scope) {}

ExternalProcessorStreamPtr ExternalProcessorClientImpl::start(
    ExternalProcessorCallbacks& callbacks,
    const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
    const Http::AsyncClient::StreamOptions& options,
    Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) {
  auto client_or_error =
      client_manager_.getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, scope_, true);
  THROW_IF_NOT_OK_REF(client_or_error.status());
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> grpcClient(client_or_error.value());
  return ExternalProcessorStreamImpl::create(std::move(grpcClient), callbacks, options,
                                             sidestream_watermark_callbacks);
}

void ExternalProcessorClientImpl::sendRequest(
    envoy::service::ext_proc::v3::ProcessingRequest&& request, bool end_stream, const uint64_t,
    RequestCallbacks*, StreamBase* stream) {
  ExternalProcessorStream* grpc_stream = dynamic_cast<ExternalProcessorStream*>(stream);
  grpc_stream->send(std::move(request), end_stream);
}

ExternalProcessorStreamPtr ExternalProcessorStreamImpl::create(
    Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
    ExternalProcessorCallbacks& callbacks, const Http::AsyncClient::StreamOptions& options,
    Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) {
  auto stream =
      std::unique_ptr<ExternalProcessorStreamImpl>(new ExternalProcessorStreamImpl(callbacks));

  if (stream->startStream(std::move(client), options)) {
    if (stream->grpcSidestreamFlowControl()) {
      stream->stream_->setWatermarkCallbacks(sidestream_watermark_callbacks);
    }
    return stream;
  }
  // Return nullptr on the start failure.
  return nullptr;
}

bool ExternalProcessorStreamImpl::startStream(
    Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
    const Http::AsyncClient::StreamOptions& options) {
  client_ = std::move(client);
  auto descriptor = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(kExternalMethod);
  grpc_context_ = options.parent_context;
  stream_ = client_.start(*descriptor, *this, options);
  // Returns true if the start succeeded and returns false on start failure.
  return stream_ != nullptr;
}

void ExternalProcessorStreamImpl::send(envoy::service::ext_proc::v3::ProcessingRequest&& request,
                                       bool end_stream) {
  stream_.sendMessage(std::move(request), end_stream);
}

// TODO(tyxia) Refactor the logic of close() function. Invoking it when stream is already closed
// is redundant.
bool ExternalProcessorStreamImpl::close() {
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

void ExternalProcessorStreamImpl::onReceiveMessage(ProcessingResponsePtr&& response) {
  if (!callbacks_.has_value()) {
    ENVOY_LOG(debug, "Underlying filter object has been destroyed.");
    return;
  }
  callbacks_->onReceiveMessage(std::move(response));
}

void ExternalProcessorStreamImpl::onCreateInitialMetadata(Http::RequestHeaderMap&) {}
void ExternalProcessorStreamImpl::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {}
void ExternalProcessorStreamImpl::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) {}

void ExternalProcessorStreamImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                                const std::string& message) {
  ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status, message);
  stream_closed_ = true;

  if (!callbacks_.has_value()) {
    ENVOY_LOG(debug, "Underlying filter object has been destroyed.");
    return;
  }

  callbacks_->logGrpcStreamInfo();
  if (status == Grpc::Status::Ok) {
    callbacks_->onGrpcClose();
  } else {
    callbacks_->onGrpcError(status);
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
