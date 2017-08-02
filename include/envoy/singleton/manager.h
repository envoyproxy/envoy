#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/singleton/instance.h"

namespace Envoy {
namespace Singleton {

/**
 * An abstract registration for a singleton entry.
 */
class Registration {
public:
  virtual ~Registration() {}
  virtual std::string name() PURE;
};

/**
 * A concrete implementation of a singleton registration. All singletons are referenced by name
 * and must be statically registered ahead of time. This can be done like so:
 *
 * static const char foo_singleton_name[] = "foo_singleton";
 * static Registry::RegisterFactory<Singleton::RegistrationImpl<foo_singleton_name>,
 *                                  Singleton::Registration>
 *     date_provider_singleton_registered_;
 *
 * Once this is done, the singleton can be get/set via the manager. See the Manager interface
 * for more information.
 */
template <const char* name_param> class RegistrationImpl : public Registration {
public:
  std::string name() override { return name_param; }
};

/**
 * A manager for all server-side singletons.
 */
class Manager {
public:
  virtual ~Manager() {}

  /**
   * This is a helper on top of get() that casts the object stored to the specified type. No type
   * information is specified explicitly in code so dynamic_cast provides some level of protection
   * via RTTI.
   */
  template <class T> std::shared_ptr<T> getTyped(const std::string& name) {
    return std::dynamic_pointer_cast<T>(get(name));
  }

  /**
   * Get a singleton.
   * @param name supplies the singleton name. Must be registered via RegistrationImpl.
   * @return InstancePtr the singleton.
   */
  virtual InstancePtr get(const std::string& name) PURE;

  /**
   * Set a singleton.
   * @param name supplies the singleton name. Must be registered via RegistrationImpl.
   * @param singleton supplies the singleton. NOTE: The manager only stores a weak pointer. This
   *        allows a singleton to be cleaned up if it is not needed any more. All code that uses
   *        singletons must check for validity and create a new singleton if needed. It must also
   *        store the shared_ptr for as long as the singleton is needed.
   */
  virtual void set(const std::string& name, InstancePtr singleton) PURE;
};

typedef std::unique_ptr<Manager> ManagerPtr;

} // namespace Singleton
} // namespace Envoy
