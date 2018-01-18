#include "common/grpc/async_client_manager_impl.h"

#include "common/grpc/async_client_impl.h"

namespace Envoy {
namespace Grpc {

AsyncClientFactoryImpl::AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                                               const std::string& cluster_name)
    : cm_(cm), cluster_name_(cluster_name) {
  auto clusters = cm_.clusters();
  const auto& it = clusters.find(cluster_name_);
  if (it == clusters.end()) {
    throw EnvoyException(fmt::format("Unknown gRPC client cluster '{}'", cluster_name_));
  }
  if (it->second.get().info()->addedViaApi()) {
    throw EnvoyException(fmt::format("gRPC client cluster '{}' is not static", cluster_name_));
  }
}

AsyncClientPtr AsyncClientFactoryImpl::create() {
  return std::make_unique<AsyncClientImpl>(cm_, cluster_name_);
}

AsyncClientManagerImpl::AsyncClientManagerImpl(Upstream::ClusterManager& cm,
                                               ThreadLocal::Instance& /*tls*/)
    : cm_(cm) {}

AsyncClientFactoryPtr
AsyncClientManagerImpl::factoryForGrpcService(const envoy::api::v2::GrpcService& grpc_service,
                                              Stats::Scope& /*scope*/) {
  switch (grpc_service.target_specifier_case()) {
  case envoy::api::v2::GrpcService::kEnvoyGrpc:
    return std::make_unique<AsyncClientFactoryImpl>(cm_, grpc_service.envoy_grpc().cluster_name());
  case envoy::api::v2::GrpcService::kGoogleGrpc:
    throw EnvoyException("Google C++ gRPC client is not implemented yet");
  default:
    NOT_REACHED;
  }
  return nullptr;
}

} // namespace Grpc
} // namespace Envoy
