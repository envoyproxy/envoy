#include "source/extensions/filters/common/ext_proc/grpc_client.h"
#include "source/extensions/filters/common/ext_proc/grpc_stream.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExtProc {

template <typename RequestType, typename ResponseType>
ProcessorStreamPtr<RequestType, ResponseType> ProcessorClientImpl<RequestType, ResponseType>::start(
    ProcessorCallbacks<RequestType, ResponseType>& callbacks,
    const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
    Http::AsyncClient::StreamOptions& options,
    Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) {
  auto client_or_error =
      client_manager_.getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, scope_, true);
  THROW_IF_NOT_OK_REF(client_or_error.status());
  Grpc::AsyncClient<RequestType, ResponseType> grpcClient(client_or_error.value());
  return ProcessorStreamImpl<RequestType, ResponseType>::create(
      std::move(grpcClient), callbacks, options, sidestream_watermark_callbacks, service_method_);
}

template <typename RequestType, typename ResponseType>
void ProcessorClientImpl<RequestType, ResponseType>::sendRequest(
    RequestType&& request, bool end_stream, const uint64_t,
    RequestCallbacks<RequestType, ResponseType>* callbacks,
    StreamBase<RequestType, ResponseType>* stream) {
  auto* grpc_stream = dynamic_cast<ProcessorStream<RequestType, ResponseType>*>(stream);
  if (grpc_stream != nullptr) {
    grpc_stream->send(std::move(request), end_stream);
  }
}

// Explicit template instantiations for the types we use
// This will be extended for your network service types
template class ProcessorClientImpl<envoy::service::ext_proc::v3::ProcessingRequest,
                                  envoy::service::ext_proc::v3::ProcessingResponse>;

} // namespace ExtProc
} // namespace Common
} // namespace Extensions
} // namespace Envoy
