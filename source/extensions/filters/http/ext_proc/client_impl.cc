#include "extensions/filters/http/ext_proc/client_impl.h"

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
ExternalProcessorClientImpl::start(ExternalProcessorCallbacks& callbacks,
                                   const std::chrono::milliseconds& timeout) {
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> grpcClient(factory_->create());
  return std::make_unique<ExternalProcessorStreamImpl>(std::move(grpcClient), callbacks, timeout);
}

ExternalProcessorStreamImpl::ExternalProcessorStreamImpl(
    Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
    ExternalProcessorCallbacks& callbacks, const std::chrono::milliseconds& timeout)
    : callbacks_(callbacks) {
  client_ = std::move(client);
  auto descriptor = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(kExternalMethod);
  Http::AsyncClient::StreamOptions options;
  options.setTimeout(timeout);

  stream_ = client_.start(*descriptor, *this, options);
}

void ExternalProcessorStreamImpl::send(
    envoy::service::ext_proc::v3alpha::ProcessingRequest&& request, bool end_stream) {
  stream_.sendMessage(std::move(request), end_stream);
}

void ExternalProcessorStreamImpl::close() { stream_->closeStream(); }

void ExternalProcessorStreamImpl::onReceiveMessage(ProcessingResponsePtr&& response) {
  callbacks_.onReceiveMessage(std::move(response));
}

void ExternalProcessorStreamImpl::onCreateInitialMetadata(Http::RequestHeaderMap&) {}
void ExternalProcessorStreamImpl::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {}
void ExternalProcessorStreamImpl::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) {}

void ExternalProcessorStreamImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                                const std::string&) {
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
