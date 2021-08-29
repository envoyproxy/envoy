#include "source/extensions/filters/http/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

static constexpr char kExternalMethod[] =
    "envoy.service.ext_proc.v3alpha.ExternalProcessor.Process";

ExternalProcessorClientImpl::ExternalProcessorClientImpl(
    Grpc::AsyncClientManager& client_manager,
    const envoy::config::core::v3::GrpcService& grpc_service, Stats::Scope& scope) {
  factory_ = client_manager.factoryForGrpcService(grpc_service, scope, true);
}

ExternalProcessorStreamPtr
ExternalProcessorClientImpl::start(ExternalProcessorCallbacks& callbacks) {
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> grpcClient(
      factory_->createUncachedRawAsyncClient());
  return std::make_unique<ExternalProcessorStreamImpl>(std::move(grpcClient), callbacks);
}

ExternalProcessorStreamImpl::ExternalProcessorStreamImpl(
    Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
    ExternalProcessorCallbacks& callbacks)
    : callbacks_(callbacks) {
  client_ = std::move(client);
  auto descriptor = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(kExternalMethod);
  Http::AsyncClient::StreamOptions options;
  stream_ = client_.start(*descriptor, *this, options);
}

void ExternalProcessorStreamImpl::send(
    envoy::service::ext_proc::v3alpha::ProcessingRequest&& request, bool end_stream) {
  stream_.sendMessage(std::move(request), end_stream);
}

bool ExternalProcessorStreamImpl::close() {
  if (!stream_closed_) {
    ENVOY_LOG(debug, "Closing gRPC stream");
    stream_->closeStream();
    stream_closed_ = true;
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
