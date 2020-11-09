#include "common/grpc/google_async_client_cache.h"

namespace Envoy {
namespace Grpc {

void AsyncClientCache::init(const ::envoy::config::core::v3::GrpcService& grpc_proto_config) {
  // The cache stores Google gRPC client, so channel is not created for each
  // request.
  ASSERT(grpc_proto_config.has_google_grpc());
  auto shared_this = shared_from_this();
  tls_slot_.set([shared_this, grpc_proto_config](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalCache>(shared_this->async_client_manager_,
                                              shared_this->scope_, grpc_proto_config);
  });
}

const Grpc::RawAsyncClientSharedPtr AsyncClientCache::getAsyncClient() {
  return tls_slot_->async_client_;
}

AsyncClientCacheSharedPtr AsyncClientCacheSingleton::getOrCreateAsyncClientCache(
    Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
    ThreadLocal::SlotAllocator& tls,
    const ::envoy::config::core::v3::GrpcService& grpc_proto_config) {
  const std::size_t cache_key = MessageUtil::hash(grpc_proto_config.google_grpc());
  const auto it = async_clients_.find(cache_key);
  if (it != async_clients_.end()) {
    return it->second;
  }
  AsyncClientCacheSharedPtr async_client =
      std::make_shared<AsyncClientCache>(async_client_manager, scope, tls);
  async_client->init(grpc_proto_config);
  async_clients_.emplace(cache_key, async_client);
  return async_client;
}

SINGLETON_MANAGER_REGISTRATION(google_grpc_async_client_cache);

AsyncClientCacheSingletonSharedPtr
getAsyncClientCacheSingleton(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<Envoy::Grpc::AsyncClientCacheSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(google_grpc_async_client_cache),
      [] { return std::make_shared<Envoy::Grpc::AsyncClientCacheSingleton>(); });
}

} // namespace Grpc
} // namespace Envoy
