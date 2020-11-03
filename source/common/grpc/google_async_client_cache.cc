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

} // namespace Grpc
} // namespace Envoy
