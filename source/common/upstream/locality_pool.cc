#include "source/common/upstream/locality_pool.h"

#include "envoy/config/core/v3/base.pb.h"

namespace Envoy {
namespace Upstream {

SINGLETON_MANAGER_REGISTRATION(const_locality_shared_pool);

ConstLocalitySharedPoolSharedPtr
LocalityPool::getConstLocalitySharedPool(Singleton::Manager& manager,
                                         Event::Dispatcher& dispatcher) {
  // Creating a pinned localities pool.
  return manager.getTyped<ConstLocalitySharedPool>(
      SINGLETON_MANAGER_REGISTERED_NAME(const_locality_shared_pool),
      [&dispatcher] { return std::make_shared<ConstLocalitySharedPool>(dispatcher); }, true);
}

} // namespace Upstream
} // namespace Envoy
