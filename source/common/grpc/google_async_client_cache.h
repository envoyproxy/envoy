#include <memory>

#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Grpc {

// The RawAsyncClient client cache for Google grpc so channel is not created
// for each request.
// TODO(fpliu233): The cache will cause resource leak that a new channel is
// created every time a new config is pushed. Improve gRPC channel cache with
// better solution.
// TODO(fpliu233): Remove when the cleaner and generic solution for gRPC is
// live. Tracking in #2598 and #13417.
class AsyncClientCache : public std::enable_shared_from_this<AsyncClientCache> {
public:
  AsyncClientCache(AsyncClientManager& async_client_manager, Stats::Scope& scope,
                   ThreadLocal::SlotAllocator& tls)
      : async_client_manager_(async_client_manager), scope_(scope), tls_slot_(tls) {}
  void init(const ::envoy::config::core::v3::GrpcService& grpc_proto_config);
  const RawAsyncClientSharedPtr getAsyncClient();

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(AsyncClientManager& async_client_manager, Stats::Scope& scope,
                     const ::envoy::config::core::v3::GrpcService& grpc_proto_config) {
      const AsyncClientFactoryPtr factory =
          async_client_manager.factoryForGrpcService(grpc_proto_config, scope, true);
      async_client_ = factory->create();
    }
    RawAsyncClientSharedPtr async_client_;
  };

  AsyncClientManager& async_client_manager_;
  Stats::Scope& scope_;
  ThreadLocal::TypedSlot<ThreadLocalCache> tls_slot_;
};

using AsyncClientCacheSharedPtr = std::shared_ptr<AsyncClientCache>;

} // namespace Grpc
} // namespace Envoy
