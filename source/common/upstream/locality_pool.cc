#include "source/common/upstream/locality_pool.h"

#include "envoy/config/core/v3/base.pb.h"

namespace Envoy {
namespace Upstream {

SINGLETON_MANAGER_REGISTRATION(const_locality_shared_pool);

ConstLocalitySharedPoolSharedPtr
LocalityPool::getConstLocalitySharedPool(Singleton::Manager& manager,
                                         Event::Dispatcher& dispatcher) {
  return manager.getTyped<SharedPool::ObjectSharedPool<const envoy::config.core.v3::Locality,
                                                       LocalityHash, LocalityEqualTo>>(
      SINGLETON_MANAGER_REGISTERED_NAME(const_locality_shared_pool), [&dispatcher] {
        return std::make_shared<SharedPool::ObjectSharedPool<const envoy::config.core.v3::Locality,
                                                             LocalityHash, LocalityEqualTo>>(
            dispatcher);
      });
}

} // namespace Upstream
} // namespace Envoy
