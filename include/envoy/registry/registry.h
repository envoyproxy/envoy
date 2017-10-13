#pragma once

#include <string>
#include <unordered_map>

#include "envoy/common/exception.h"

#include "fmt/format.h"
#include "fmt/ostream.h"

namespace Envoy {
namespace Registry {

/**
 * General registry for implementation factories. The registry is templated by the Base class that a
 * set of factories conforms to.
 *
 * Classes are found by name, so a single name cannot be registered twice for the same Base class.
 * Factories are registered by reference and this reference is expected to be valid through the life
 * of the program. Factories cannot be deregistered.
 * Factories should generally be registered by statically instantiating the RegisterFactory class.
 *
 * Note: This class is not thread safe, so registration should only occur in a single threaded
 * environment, which is guaranteed by the static instantiation mentioned above.
 *
 * Exaple lookup: BaseFactoryType *factory =
 * FactoryRegistry<BaseFactoryType>::getFactory("example_factory_name");
 */
template <class Base> class FactoryRegistry {
public:
  static void registerFactory(Base& factory) {
    auto result = factories().emplace(std::make_pair(factory.name(), &factory));
    if (!result.second) {
      throw EnvoyException(fmt::format("Double registration for name: '{}'", factory.name()));
    }
  }

  /**
   * Gets a factory by name.  If the name isn't found in the registry, returns nullptr.
   */
  static Base* getFactory(const std::string& name) {
    auto it = factories().find(name);
    if (it == factories().end()) {
      return nullptr;
    }
    return it->second;
  }

  /**
   * Remove a factory by name.
   */
  static void unregisterFactory(Base& factory) {
    auto result = factories().erase(factory.name());
    if (result == 0) {
      throw EnvoyException(fmt::format("No registration for name: '{}'", factory.name()));
    }
  }

private:
  /**
   * Gets the current map of factory implementations.
   */
  static std::unordered_map<std::string, Base*>& factories() {
    static std::unordered_map<std::string, Base*>* factories =
        new std::unordered_map<std::string, Base*>;
    return *factories;
  }
};

/**
 * Factory registration template. Enables users to register a particular implementation factory with
 * the FactoryRegistry by instantiating this templated class with the specific factory class and the
 * general Base class to which that factory conforms.
 *
 * Because factories are generally registered once and live for the length of the program, the
 * standard use of this class is static instantiation within a linked implementation's translation
 * unit. For an example of a typical use case, @see NamedNetworkFilterConfigFactory.
 *
 * Example registration: static Registry::RegisterFactory<SpecificFactory, BaseFactory> registered_;
 */
template <class T, class Base> class RegisterFactory {
public:
  /**
   * Contructor that registers an instance of the factory with the FactoryRegistry.
   */
  RegisterFactory() { FactoryRegistry<Base>::registerFactory(instance_); }

  /**
   * Destructor that removes an instance of the factory from the FactoryRegistry.
   */
  ~RegisterFactory() { FactoryRegistry<Base>::unregisterFactory(instance_); }

  T& testGetFactory() { return instance_; }

private:
  T instance_;
};

} // namespace Registry
} // namespace Envoy
