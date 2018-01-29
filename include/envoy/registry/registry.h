#pragma once

#include <string>
#include <unordered_map>

#include "envoy/common/exception.h"

#include "common/common/fmt.h"

namespace Envoy {
namespace Registry {

// Forward declaration of test class for friend declaration below.
template <typename T> class InjectFactory;

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
   * Gets a factory by name. If the name isn't found in the registry, returns nullptr.
   */
  static Base* getFactory(const std::string& name) {
    auto it = factories().find(name);
    if (it == factories().end()) {
      return nullptr;
    }
    return it->second;
  }

private:
  // Allow factory injection only in tests.
  friend class InjectFactory<Base>;

  /**
   * Replaces a factory by name. This method should only be used for testing purposes.
   * @param factory is the factory to inject.
   * @return Base* a pointer to the previously registered value.
   */
  static Base* replaceFactoryForTest(Base& factory) {
    auto displaced = getFactory(factory.name());
    factories().emplace(std::make_pair(factory.name(), &factory));
    return displaced;
  }

  /**
   * Remove a factory by name. This method should only be used for testing purposes.
   * @param name is the name of the factory to remove.
   */
  static void removeFactoryForTest(const std::string& name) {
    auto result = factories().erase(name);
    if (result == 0) {
      throw EnvoyException(fmt::format("No registration for name: '{}'", name));
    }
  }

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

private:
  T instance_{};
};

} // namespace Registry
} // namespace Envoy
