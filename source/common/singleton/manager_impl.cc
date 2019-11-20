#include "common/singleton/manager_impl.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Singleton {

InstanceSharedPtr ManagerImpl::get(const std::string& name, SingletonFactoryCb cb) {
  ASSERT(run_tid_ == thread_factory_.currentThreadId());

  if (nullptr == Registry::FactoryRegistry<Registration>::getFactory(name)) {
    PANIC(fmt::format("invalid singleton name '{}'. Make sure it is registered.", name));
  }

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
