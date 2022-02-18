#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/repeated_field.h>

#include <memory>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/init/target_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_json_transcoder/async_reflection_fetcher_callbacks.h"

#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

using ServerReflectionResponsePtr =
    std::unique_ptr<grpc::reflection::v1alpha::ServerReflectionResponse>;

/**
 * This class contains the logic for starting gRPC reflection Rpcs and tracking their completion
 * state.
 */
class AsyncReflectionFetcher : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Constructor. All arguments are needed for making non-blocking gRpc
   * requests with the Envoy client.
   */
  AsyncReflectionFetcher(const envoy::extensions::filters::http::grpc_json_transcoder::v3::
                             GrpcJsonTranscoder_ReflectionConfig& reflection_cluster_config,
                         const Envoy::Protobuf::RepeatedPtrField<std::string>& services,
                         std::vector<Grpc::RawAsyncClientSharedPtr> async_clients,
                         Init::Manager& init_manager)
      : reflection_cluster_config_(reflection_cluster_config), init_manager_(init_manager),
        services_(std::begin(services), std::end(services)), async_clients_() {
    // Wrap RawAsyncClient into AsyncClient.
    for (size_t i = 0; i < async_clients.size(); i++) {
      async_clients_.emplace_back(async_clients[i]);
    }
  }

  /**
   * Ask this AsyncReflectionFetcher to call descriptor_pool_builder.
   */
  void requestFileDescriptors(
      std::function<void(Envoy::Protobuf::FileDescriptorSet)> file_descriptors_available);

  /**
   * Kick off reflection Rpcs; invoked by initManager.
   */
  void startReflectionRpcs();

  /**
   * Callback for successful completion of an Rpc.
   */
  void onRpcCompleted(AsyncReflectionFetcherCallbacks* callbacks,
                      ServerReflectionResponsePtr&& message);

  /**
   * Retrieve remaining callbacks. Used only for unit testing.
   */
  absl::flat_hash_set<AsyncReflectionFetcherCallbacks*>& getRemainingCallBacksForTest() {
    return remaining_callbacks_;
  }

private:
  /**
   * The proto config for talking to the upstream Envoy cluster serving gRPC.
   */
  const envoy::extensions::filters::http::grpc_json_transcoder::v3::
      GrpcJsonTranscoder_ReflectionConfig reflection_cluster_config_;

  /**
   * Reference to the factory context's init manager, used for deferring
   * completion of filter initialization until the reflection Rpcs complete.
   */
  Init::Manager& init_manager_;

  /**
   * A function to call when the requested file descriptors are available.
   */
  std::function<void(Envoy::Protobuf::FileDescriptorSet)>
      file_descriptors_available_ ABSL_GUARDED_BY(receive_message_mutex_);

  /**
   * The fully qualified names of the gRPC services we are requesting protos
   * for.
   */
  std::vector<std::string> services_;

  /**
   * The callback objects for the remaining Rpcs that we are waiting on
   * responses for.
   */
  absl::flat_hash_set<AsyncReflectionFetcherCallbacks*> remaining_callbacks_;

  /**
   * The callback objects for all the Rpcs that we have made. This is used to
   * maintain the lifetime of the callbacks_ object, while remaining_callbacks_
   * is used to track whether all our Rpcs have completed. We cannot use
   * remaining_callbacks_ for lifetime because we want to enable the filter
   * after the last onReceiveMessage is invoked, but cannot delete the callback
   * object at this time, because this is not the last callback that will be
   * invoked by the Envoy Grpc Client.
   *
   * Additionally, separating ownership from completion tracking avoids the
   * need to abuse unique_ptr release semantics to erase from a set of
   * unique_ptrs.
   */
  absl::flat_hash_set<std::unique_ptr<AsyncReflectionFetcherCallbacks>> callbacks_;

  /**
   * The async clients used to make each server reflection request. We are
   * using multiple clients to send multiple streaming Rpcs in parallel, as
   * recommended by qiwzhang.
   */
  std::vector<Envoy::Grpc::AsyncClient<grpc::reflection::v1alpha::ServerReflectionRequest,
                                       grpc::reflection::v1alpha::ServerReflectionResponse>>
      async_clients_;

  /**
   * The target for the initManager, used to notify the init manager that the
   * filter has finished initializing.
   */
  std::unique_ptr<Init::TargetImpl> init_target_;

  /**
   * The file descriptors we have received from reflection requests thus far.
   */
  Envoy::Protobuf::FileDescriptorSet file_descriptor_set_ ABSL_GUARDED_BY(receive_message_mutex_);

  /**
   * This mutex protects the data structures manipulated in the
   * onReceiveMessage callback, since the same object is used for handling all
   * the callbacks.
   */
  Envoy::Thread::MutexBasicLockable receive_message_mutex_;
};

using AsyncReflectionFetcherSharedPtr = std::shared_ptr<AsyncReflectionFetcher>;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
