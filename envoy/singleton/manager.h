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
   * @param name the unique name of the singleton instance. This should be provided by the macro
   * SINGLETON_MANAGER_REGISTERED_NAME.
   * @param cb supplies the singleton creation callback. This will only be called if the singleton
   * does not already exist.
   * @param pin supplies whether the singleton should be pinned. By default, the manager only stores
   * a weak pointer. This allows a singleton to be cleaned up if it is not needed any more. All code
   * that uses singletons must store the shared_ptr for as long as the singleton is needed. But if
   * the pin is set to true, the singleton will be stored as a shared_ptr. This is useful if the
   * users want to keep the singleton around for the lifetime of the server even if it is not used
   * for a while.
   * @return InstancePtr the singleton cast to the specified type. nullptr if the singleton does not
   * exist.
   */
  template <class T>
  std::shared_ptr<T> getTyped(const std::string& name, SingletonFactoryCb cb, bool pin = false) {
    return std::dynamic_pointer_cast<T>(get(name, cb, pin));
  }

  /**
   * This is a non-constructing getter. Use when the caller can deal with instances where
   * the singleton being accessed may not have been constructed previously.
   * @param name the unique name of singleton instance. This should be provided by the macro
   * SINGLETON_MANAGER_REGISTERED_NAME.
   * @return InstancePtr the singleton cast to the specified type. nullptr if the singleton does not
   * exist.
   */
  template <class T> std::shared_ptr<T> getTyped(const std::string& name) {
    return std::dynamic_pointer_cast<T>(get(
        name, [] { return nullptr; }, false));
  }

  /**
   * Get a singleton and create it if it does not exist.
   * @param name the unique name of the singleton instance. This should be provided by the macro
   * SINGLETON_MANAGER_REGISTERED_NAME.
   * @param cb supplies the singleton creation callback. This will only be called if the singleton
   * does not already exist.
   * @param pin supplies whether the singleton should be pinned. By default, the manager only stores
   * a weak pointer. This allows a singleton to be cleaned up if it is not needed any more. All code
   * that uses singletons must store the shared_ptr for as long as the singleton is needed. But if
   * the pin is set to true, the singleton will be stored as a shared_ptr. This is useful if the
   * users want to keep the singleton around for the lifetime of the server even if it is not used
   * for a while.
   * @return InstancePtr the singleton cast to the specified type. nullptr if the singleton does not
   * exist.
   */
  virtual InstanceSharedPtr get(const std::string& name, SingletonFactoryCb cb, bool pin) PURE;
};

using ManagerPtr = std::unique_ptr<Manager>;

} // namespace Singleton
} // namespace Envoy
