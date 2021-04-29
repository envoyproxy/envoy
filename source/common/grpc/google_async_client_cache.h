#pragma once

#include <memory>

#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Grpc {

// The RawAsyncClient client cache for Google grpc so channel is not created
// for each request.
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

class AsyncClientCacheSingleton : public Singleton::Instance {
public:
  AsyncClientCacheSingleton() = default;
  AsyncClientCacheSharedPtr
  getOrCreateAsyncClientCache(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                              ThreadLocal::SlotAllocator& tls,
                              const ::envoy::config::core::v3::GrpcService& grpc_proto_config);

private:
  // The tls slots with client cache stored with key as hash of
  // envoy::config::core::v3::GrpcService::GoogleGrpc config.
  // TODO(fpliu233): When new configuration is pushed, the old client cache will not get cleaned up.
  // Remove when the cleaner and generic solution for gRPC is live. Tracking in #2598 and #13417.
  absl::flat_hash_map<std::size_t, AsyncClientCacheSharedPtr> async_clients_;
};

using AsyncClientCacheSingletonSharedPtr = std::shared_ptr<AsyncClientCacheSingleton>;

AsyncClientCacheSingletonSharedPtr
getAsyncClientCacheSingleton(Server::Configuration::ServerFactoryContext& context);

} // namespace Grpc
} // namespace Envoy
