#include "source/server/instance_impl.h"

#include "source/server/guarddog_impl.h"
#include "source/server/overload_manager_impl.h"

namespace Envoy {
namespace Server {
void InstanceImpl::maybeCreateHeapShrinker() {
  heap_shrinker_ =
      std::make_unique<Memory::HeapShrinker>(dispatcher(), overloadManager(), *stats().rootScope());
}

std::unique_ptr<OverloadManager> InstanceImpl::createOverloadManager() {
  return std::make_unique<OverloadManagerImpl>(
      dispatcher(), *stats().rootScope(), threadLocal(), bootstrap().overload_manager(),
      messageValidationContext().staticValidationVisitor(), api(), options());
}

std::unique_ptr<Server::GuardDog> InstanceImpl::maybeCreateGuardDog(absl::string_view name) {
  return std::make_unique<Server::GuardDogImpl>(*stats().rootScope(),
                                                config().mainThreadWatchdogConfig(), api(), name);
}

} // namespace Server
} // namespace Envoy
