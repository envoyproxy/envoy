#include "source/server/instance_impl.h"

#include "source/common/upstream/health_discovery_service.h"
#include "source/server/guarddog_impl.h"
#include "source/server/null_overload_manager.h"
#include "source/server/overload_manager_impl.h"

namespace Envoy {
namespace Server {
void InstanceImpl::maybeCreateHeapShrinker() {
  heap_shrinker_ =
      std::make_unique<Memory::HeapShrinker>(dispatcher(), overloadManager(), *stats().rootScope());
}

absl::StatusOr<std::unique_ptr<OverloadManager>> InstanceImpl::createOverloadManager() {
  return OverloadManagerImpl::create(
      dispatcher(), *stats().rootScope(), threadLocal(), bootstrap().overload_manager(),
      messageValidationContext().staticValidationVisitor(), api(), options());
}

std::unique_ptr<OverloadManager> InstanceImpl::createNullOverloadManager() {
  return std::make_unique<NullOverloadManager>(threadLocal(), false);
}

std::unique_ptr<Server::GuardDog> InstanceImpl::maybeCreateGuardDog(absl::string_view name) {
  return std::make_unique<Server::GuardDogImpl>(*stats().rootScope(),
                                                config().mainThreadWatchdogConfig(), api(), name);
}

std::unique_ptr<HdsDelegateApi> InstanceImpl::maybeCreateHdsDelegate(
    Configuration::ServerFactoryContext& server_context, Stats::Scope& scope,
    Grpc::RawAsyncClientPtr&& async_client, Envoy::Stats::Store& stats,
    Ssl::ContextManager& ssl_context_manager, Upstream::ClusterInfoFactory& info_factory) {
  return std::make_unique<Upstream::HdsDelegate>(server_context, scope, std::move(async_client),
                                                 stats, ssl_context_manager, info_factory);
}

} // namespace Server
} // namespace Envoy
