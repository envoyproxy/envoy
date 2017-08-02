#include "common/singleton/manager_impl.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Singleton {

InstancePtr ManagerImpl::get(const std::string& name) {
  verifyRegistration(name);
  return singletons_[name].lock();
}

void ManagerImpl::set(const std::string& name, InstancePtr singleton) {
  verifyRegistration(name);
  singletons_[name] = singleton;
}

void ManagerImpl::verifyRegistration(const std::string& name) {
  if (nullptr == Registry::FactoryRegistry<Registration>::getFactory(name)) {
    throw EnvoyException(
        fmt::format("invalid singleton name '{}'. Make sure it is registered.", name));
  }
}

} // namespace Singleton
} // namespace Envoy
