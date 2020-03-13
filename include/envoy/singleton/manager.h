#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/instance.h"

namespace Envoy {
namespace Singleton {

/**
 * An abstract registration for a singleton entry.
 */
class Registration : public Config::UntypedFactory {
public:
  virtual ~Registration() = default;
  std::string category() const override { return "envoy.singleton"; }
};

/**
 * A concrete implementation of a singleton registration. All singletons are referenced by name
 * and must be statically registered ahead of time. This can be done like so:
 *
 * static constexpr char foo_singleton_name[] = "foo_singleton";
 * static Registry::RegisterFactory<Singleton::RegistrationImpl<foo_singleton_name>,
 *                                  Singleton::Registration>
 *     date_provider_singleton_registered_;
 *
 * Once this is done, the singleton can be get/set via the manager. See the Manager interface
 * for more information.
 */
template <const char* name_param> class RegistrationImpl : public Registration {
public:
  std::string name() const override { return name_param; }
};

/**
 * Macro used to statically register singletons managed by the singleton manager
 * defined in envoy/singleton/manager.h. After the NAME has been registered use the
 * SINGLETON_MANAGER_REGISTERED_NAME macro to access the name registered with the
 * singleton manager.
 */
#define SINGLETON_MANAGER_REGISTRATION(NAME)                                                       \
  static constexpr char NAME##_singleton_name[] = #NAME "_singleton";                              \
  static Envoy::Registry::RegisterInternalFactory<                                                 \
      Envoy::Singleton::RegistrationImpl<NAME##_singleton_name>, Envoy::Singleton::Registration>   \
      NAME##_singleton_registered_;

#define SINGLETON_MANAGER_REGISTERED_NAME(NAME) NAME##_singleton_name

/**
 * Callback function used to create a singleton.
 */
using SingletonFactoryCb = std::function<InstanceSharedPtr()>;

/**
 * A manager for all server-side singletons.
 */
class Manager {
public:
  virtual ~Manager() = default;

  /**
   * This is a helper on top of get() that casts the object stored to the specified type. Since the
   * manager only stores pointers to the base interface, dynamic_cast provides some level of
   * protection via RTTI.
   */
  template <class T> std::shared_ptr<T> getTyped(const std::string& name, SingletonFactoryCb cb) {
    return std::dynamic_pointer_cast<T>(get(name, cb));
  }

  /**
   * Get a singleton and create it if it does not exist.
   * @param name supplies the singleton name. Must be registered via RegistrationImpl.
   * @param singleton supplies the singleton creation callback. This will only be called if the
   *        singleton does not already exist. NOTE: The manager only stores a weak pointer. This
   *        allows a singleton to be cleaned up if it is not needed any more. All code that uses
   *        singletons must store the shared_ptr for as long as the singleton is needed.
   * @return InstancePtr the singleton.
   */
  virtual InstanceSharedPtr get(const std::string& name, SingletonFactoryCb) PURE;
};

using ManagerPtr = std::unique_ptr<Manager>;

} // namespace Singleton
} // namespace Envoy
