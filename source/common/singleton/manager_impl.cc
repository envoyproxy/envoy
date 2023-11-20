#include "source/common/singleton/manager_impl.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Singleton {

InstanceSharedPtr ManagerImpl::get(const std::string& name, SingletonFactoryCb cb) {
  ASSERT(run_tid_ == thread_factory_.currentThreadId());

  ENVOY_BUG(Registry::FactoryRegistry<Registration>::getFactory(name) != nullptr,
            "invalid singleton name '" + name + "'. Make sure it is registered.");

  if (nullptr == singletons_[name].lock()) {
    InstanceSharedPtr singleton = cb();
    singletons_[name] = singleton;
    return singleton;
  } else {
    return singletons_[name].lock();
  }
}

} // namespace Singleton
} // namespace Envoy
