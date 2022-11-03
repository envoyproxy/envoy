#include "source/extensions/filters/http/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

static constexpr char kExternalMethod[] = "envoy.service.ext_proc.v3.ExternalProcessor.Process";

ExternalProcessorClientImpl::ExternalProcessorClientImpl(Grpc::AsyncClientManager& client_manager,
                                                         Stats::Scope& scope)
    : client_manager_(client_manager), scope_(scope) {}

ExternalProcessorStreamPtr
ExternalProcessorClientImpl::start(ExternalProcessorCallbacks& callbacks,
                                   const envoy::config::core::v3::GrpcService& grpc_service,
                                   const StreamInfo::StreamInfo& stream_info) {
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> grpcClient(
      client_manager_.getOrCreateRawAsyncClient(grpc_service, scope_, true));
  return std::make_unique<ExternalProcessorStreamImpl>(std::move(grpcClient), callbacks,
                                                       stream_info);
}

ExternalProcessorStreamImpl::ExternalProcessorStreamImpl(
    Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
    ExternalProcessorCallbacks& callbacks, const StreamInfo::StreamInfo& stream_info)
    : callbacks_(callbacks) {
  client_ = std::move(client);
  auto descriptor = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(kExternalMethod);
  grpc_context_.stream_info = &stream_info;
  Http::AsyncClient::StreamOptions options;
  options.setParentContext(grpc_context_);
  stream_ = client_.start(*descriptor, *this, options);
}

void ExternalProcessorStreamImpl::send(envoy::service::ext_proc::v3::ProcessingRequest&& request,
                                       bool end_stream) {
  stream_.sendMessage(std::move(request), end_stream);
}

bool ExternalProcessorStreamImpl::close() {
  if (!stream_closed_) {
    ENVOY_LOG(debug, "Closing gRPC stream");
    stream_.closeStream();
    stream_closed_ = true;
    stream_.resetStream();
    return true;
  }
  return false;
}

void ExternalProcessorStreamImpl::onReceiveMessage(ProcessingResponsePtr&& response) {
  callbacks_.onReceiveMessage(std::move(response));
}

void ExternalProcessorStreamImpl::onCreateInitialMetadata(Http::RequestHeaderMap&) {}
void ExternalProcessorStreamImpl::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {}
void ExternalProcessorStreamImpl::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) {}

void ExternalProcessorStreamImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                                const std::string& message) {
  ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status, message);
  stream_closed_ = true;
  if (status == Grpc::Status::Ok) {
    callbacks_.onGrpcClose();
  } else {
    callbacks_.onGrpcError(status);
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
