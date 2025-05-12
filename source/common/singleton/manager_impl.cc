#include "source/common/singleton/manager_impl.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Singleton {

InstanceSharedPtr ManagerImpl::get(const std::string& name, SingletonFactoryCb cb, bool pin) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  ENVOY_BUG(Registry::FactoryRegistry<Registration>::getFactory(name) != nullptr,
            "invalid singleton name '" + name + "'. Make sure it is registered.");

  auto existing_singleton = singletons_[name].lock();

  if (existing_singleton == nullptr) {
    InstanceSharedPtr singleton = cb();
    singletons_[name] = singleton;
    if (pin && singleton != nullptr) {
      pinned_singletons_.push_back(singleton);
    }
    return singleton;
  } else {
    return existing_singleton;
  }
}

} // namespace Singleton
} // namespace Envoy
