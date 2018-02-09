#include "common/grpc/async_client_manager_impl.h"

#include "common/grpc/async_client_impl.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "common/grpc/google_async_client_impl.h"
#endif

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

AsyncClientManagerImpl::AsyncClientManagerImpl(Upstream::ClusterManager& cm,
                                               ThreadLocal::Instance& tls)
    : cm_(cm), tls_(tls) {
#ifdef ENVOY_GOOGLE_GRPC
  google_tls_slot_ = tls.allocateSlot();
  google_tls_slot_->set(
      [](Event::Dispatcher&) { return std::make_shared<GoogleAsyncClientThreadLocal>(); });
#endif
}

AsyncClientPtr AsyncClientFactoryImpl::create() {
  return std::make_unique<AsyncClientImpl>(cm_, cluster_name_);
}

GoogleAsyncClientFactoryImpl::GoogleAsyncClientFactoryImpl(
    ThreadLocal::Instance& tls, ThreadLocal::Slot& google_tls_slot, Stats::Scope& scope,
    const envoy::api::v2::core::GrpcService::GoogleGrpc& config)
    : tls_(tls), google_tls_slot_(google_tls_slot),
      scope_(scope.createScope(fmt::format("grpc.{}.", config.stat_prefix()))), config_(config) {
#ifndef ENVOY_GOOGLE_GRPC
  UNREFERENCED_PARAMETER(tls_);
  UNREFERENCED_PARAMETER(scope_);
  UNREFERENCED_PARAMETER(config_);
  throw EnvoyException("Google C++ gRPC client is not linked");
#endif
}

AsyncClientPtr GoogleAsyncClientFactoryImpl::create() {
#ifdef ENVOY_GOOGLE_GRPC
  GoogleGenericStubFactory stub_factory;
  return std::make_unique<GoogleAsyncClientImpl>(
      tls_.dispatcher(), google_tls_slot_.getTyped<GoogleAsyncClientThreadLocal>(), stub_factory,
      *scope_, config_);
#else
  return nullptr;
#endif
}

AsyncClientFactoryPtr
AsyncClientManagerImpl::factoryForGrpcService(const envoy::api::v2::core::GrpcService& grpc_service,
                                              Stats::Scope& scope) {
  switch (grpc_service.target_specifier_case()) {
  case envoy::api::v2::core::GrpcService::kEnvoyGrpc:
    return std::make_unique<AsyncClientFactoryImpl>(cm_, grpc_service.envoy_grpc().cluster_name());
  case envoy::api::v2::core::GrpcService::kGoogleGrpc:
    return std::make_unique<GoogleAsyncClientFactoryImpl>(tls_, *google_tls_slot_, scope,
                                                          grpc_service.google_grpc());
  default:
    NOT_REACHED;
  }
  return nullptr;
}

} // namespace Grpc
} // namespace Envoy
