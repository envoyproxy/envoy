#include "source/extensions/filters/http/grpc_json_transcoder/async_reflection_fetcher.h"

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/common/lock_guard.h"
#include "source/common/grpc/async_client_impl.h"

using grpc::reflection::v1alpha::ServerReflection;
using grpc::reflection::v1alpha::ServerReflectionRequest;
using grpc::reflection::v1alpha::ServerReflectionResponse;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

void AsyncReflectionFetcher::requestFileDescriptors(
    std::function<void(Envoy::Protobuf::FileDescriptorSet)> file_descriptors_available) {
  file_descriptors_available_ = file_descriptors_available;
  // Register to init_manager, force the listener to wait for completion of the
  // reflection Rpcs.
  init_target_ = std::make_unique<Init::TargetImpl>("JsonGrpcFilter: Grpc Reflection Rpcs",
                                                    [this]() -> void { startReflectionRpcs(); });
  init_manager_.add(*init_target_);
}

void AsyncReflectionFetcher::startReflectionRpcs() {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name(reflection_cluster_config_.cluster_name());

  const Envoy::Protobuf::MethodDescriptor* server_reflection_method =
      Envoy::Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "grpc.reflection.v1alpha.ServerReflection.ServerReflectionInfo");
  if (server_reflection_method == nullptr) {
    ENVOY_LOG(error, "Failed to find MethodDescriptor for ServerReflectionInfo");
    return;
  }

  for (size_t i = 0; i < services_.size(); i++) {
    std::string service = services_[i];
    Envoy::Grpc::AsyncClient<ServerReflectionRequest, ServerReflectionResponse>& async_client =
        async_clients_[i];

    AsyncReflectionFetcherCallbacks* callbacks = new AsyncReflectionFetcherCallbacks(
        [this](AsyncReflectionFetcherCallbacks* callback, ServerReflectionResponsePtr&& message) {
          this->onRpcCompleted(callback, std::move(message));
        });
    callbacks_.emplace(callbacks);
    remaining_callbacks_.insert(callbacks);
    Http::AsyncClient::StreamOptions options;

    // Default of 5s is chosen arbitarily.
    options.setTimeout(std::chrono::milliseconds(reflection_cluster_config_.request_timeout()
                                                     ? reflection_cluster_config_.request_timeout()
                                                     : 5000));
    Envoy::Grpc::AsyncStream<ServerReflectionRequest> async_stream =
        async_client.start(*server_reflection_method, *callbacks, options);

    ServerReflectionRequest request;
    request.set_file_containing_symbol(service);
    async_stream.sendMessage(request, true);
  }
}

void AsyncReflectionFetcher::onRpcCompleted(AsyncReflectionFetcherCallbacks* callbacks,
                                            std::unique_ptr<ServerReflectionResponse>&& message) {
  Thread::LockGuard guard_(receive_message_mutex_);

  for (const std::string& file_descriptor_bytes :
       message->file_descriptor_response().file_descriptor_proto()) {
    if (!file_descriptor_set_.add_file()->ParseFromString(file_descriptor_bytes)) {
      ENVOY_LOG(error,
                "transcoding_filter: Unable to parse proto descriptor returned from reflection");
      return;
    }
  }

  // Record that we have already received the response for this Rpc.
  remaining_callbacks_.erase(callbacks);

  // We have received all the protos we have asked for, so inject them into the
  // proto config and signal to InitManager that loading is complete.
  if (remaining_callbacks_.empty()) {
    file_descriptors_available_(file_descriptor_set_);
    init_target_->ready();
  }
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
