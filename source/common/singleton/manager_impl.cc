#include "common/singleton/manager_impl.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Singleton {

InstanceSharedPtr ManagerImpl::get(const std::string& name, SingletonFactoryCb cb) {
  auto singleton = tryGet(name);
  if (!singleton) {
    singleton = cb();
    singletons_[name] = singleton;
  }

  return singleton;
}

InstanceSharedPtr ManagerImpl::tryGet(const std::string& name) {
  ASSERT(run_tid_ == Thread::Thread::currentThreadId());
  if (nullptr == Registry::FactoryRegistry<Registration>::getFactory(name)) {
    PANIC(fmt::format("invalid singleton name '{}'. Make sure it is registered.", name));
  }

  return singletons_[name].lock();
}

} // namespace Singleton
} // namespace Envoy
