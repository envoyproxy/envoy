#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Registry {

template <class Base> class FactoryRegistry;
template <class T, class Base> class RegisterFactory;

/**
 * FactoryRegistryProxy is a proxy object that provides access to the
 * static methods of a strongly-typed factory registry.
 */
class FactoryRegistryProxy {
public:
  virtual ~FactoryRegistryProxy() = default;
  virtual std::vector<absl::string_view> registeredNames() const PURE;
  virtual bool disableFactory(absl::string_view) PURE;
};

template <class Base> class FactoryRegistryProxyImpl : public FactoryRegistryProxy {
public:
  using FactoryRegistry = Envoy::Registry::FactoryRegistry<Base>;

  std::vector<absl::string_view> registeredNames() const override {
    return FactoryRegistry::registeredNames();
  }

  bool disableFactory(absl::string_view name) override {
    return FactoryRegistry::disableFactory(name);
  }
};

/**
 * BaseFactoryCategoryRegistry holds the static factory map for
 * FactoryCategoryRegistry, ensuring that friends of that class
 * cannot get non-const access to it.
 */
class BaseFactoryCategoryRegistry {
protected:
  using MapType = absl::flat_hash_map<std::string, FactoryRegistryProxy*>;

  static MapType& factories() {
    static auto* factories = new MapType();
    return *factories;
  }
};

/**
 * FactoryCategoryRegistry registers factory registries by their
 * declared category. The category is exposed by a static category()
 * method on the factory base type.
 *
 * Only RegisterFactory instances are able to register factory registries.
 */
class FactoryCategoryRegistry : public BaseFactoryCategoryRegistry {
public:
  /**
   * @return a read-only reference to the map of registered factory
   * registries.
   */
  static const MapType& registeredFactories() { return factories(); }

  /**
   * @return whether the given category name is already registered.
   */
  static bool isRegistered(absl::string_view category) {
    return factories().find(category) != factories().end();
  }

  static bool disableFactory(absl::string_view category, absl::string_view name) {
    auto registry = factories().find(category);

    if (registry != factories().end()) {
      return registry->second->disableFactory(name);
    }

    return false;
  }

private:
  // Allow RegisterFactory to register a category, but no-one else.
  // This enforces correct use of the registration machinery.
  template <class T, class Base> friend class RegisterFactory;

  static void registerCategory(const std::string& category, FactoryRegistryProxy* factoryNames) {
    auto result = factories().emplace(std::make_pair(category, factoryNames));
    RELEASE_ASSERT(result.second == true,
                   fmt::format("Double registration for category: '{}'", category));
  }
};

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
 * Example lookup: BaseFactoryType *factory =
 * FactoryRegistry<BaseFactoryType>::getFactory("example_factory_name");
 */
template <class Base> class FactoryRegistry : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Return a sorted vector of registered factory names.
   */
  static std::vector<absl::string_view> registeredNames() {
    std::vector<absl::string_view> ret;

    ret.reserve(factories().size());

    for (const auto& factory : factories()) {
      // Only publish the name of factories that have not been disabled.
      if (factory.second) {
        ret.push_back(factory.first);
      }
    }

    std::sort(ret.begin(), ret.end());

    return ret;
  }

  /**
   * Gets the current map of factory implementations.
   */
  static absl::flat_hash_map<std::string, Base*>& factories() {
    static auto* factories = new absl::flat_hash_map<std::string, Base*>;
    return *factories;
  }

  static absl::flat_hash_map<std::string, std::string>& deprecatedFactoryNames() {
    static auto* deprecated_factory_names = new absl::flat_hash_map<std::string, std::string>;
    return *deprecated_factory_names;
  }

  /**
   * instead_value are used when passed name was deprecated.
   */
  static void registerFactory(Base& factory, absl::string_view name,
                              absl::string_view instead_value = "") {
    auto result = factories().emplace(std::make_pair(name, &factory));
    if (!result.second) {
      throw EnvoyException(fmt::format("Double registration for name: '{}'", factory.name()));
    }
    if (!instead_value.empty()) {
      deprecatedFactoryNames().emplace(std::make_pair(name, instead_value));
    }
  }

  /**
   * Permanently disables the named factory by setting the corresponding
   * factory pointer to null. If the factory is registered under multiple
   * (deprecated) names, all the possible names are disabled.
   */
  static bool disableFactory(absl::string_view name) {
    const auto disable = [](absl::string_view name) -> bool {
      auto it = factories().find(name);
      if (it != factories().end()) {
        it->second = nullptr;
        return true;
      }
      return false;
    };

    // First, find the canonical name for this factory.
    absl::string_view canonicalName = canonicalFactoryName(name);

    // Next, disable the factory by all its deprecated names.
    for (const auto& entry : deprecatedFactoryNames()) {
      if (entry.second == canonicalName) {
        disable(entry.first);
      }
    }

    // Finally, disable the factory by its canonical name.
    return disable(canonicalName);
  }

  /**
   * Gets a factory by name. If the name isn't found in the registry, returns nullptr.
   */
  static Base* getFactory(absl::string_view name) {
    auto it = factories().find(name);
    if (it == factories().end()) {
      return nullptr;
    }

    checkDeprecated(name);
    return it->second;
  }

  /**
   * @return the canonical name of the factory. If the given name is a
   * deprecated factory name, the canonical name is returned instead.
   */
  static absl::string_view canonicalFactoryName(absl::string_view name) {
    const auto it = deprecatedFactoryNames().find(name);
    return (it == deprecatedFactoryNames().end()) ? name : it->second;
  }

  static void checkDeprecated(absl::string_view name) {
    auto it = deprecatedFactoryNames().find(name);
    const bool status = it != deprecatedFactoryNames().end();
    if (status) {
      ENVOY_LOG(warn, "{} is deprecated, use {} instead.", it->first, it->second);
    }
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
    auto it = factories().find(factory.name());
    Base* displaced = nullptr;
    if (it != factories().end()) {
      displaced = it->second;
      factories().erase(it);
    }

    factories().emplace(factory.name(), &factory);
    RELEASE_ASSERT(getFactory(factory.name()) == &factory, "");
    return displaced;
  }

  /**
   * Remove a factory by name. This method should only be used for testing purposes.
   * @param name is the name of the factory to remove.
   */
  static void removeFactoryForTest(absl::string_view name) {
    auto result = factories().erase(name);
    RELEASE_ASSERT(result == 1, "");
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
 * Example registration: REGISTER_FACTORY(SpecificFactory, BaseFactory);
 *                       REGISTER_FACTORY(SpecificFactory, BaseFactory){"deprecated_name"};
 */
template <class T, class Base> class RegisterFactory {
public:
  /**
   * Constructor that registers an instance of the factory with the FactoryRegistry.
   */
  RegisterFactory() {
    ASSERT(!instance_.name().empty());
    FactoryRegistry<Base>::registerFactory(instance_, instance_.name());

    // Also register this factory with its category.
    //
    // Each time a factory registers, the registry will attempt to
    // register its category here. This means that we have to ignore
    // multiple attempts to register the same category and can't detect
    // duplicate categories.
    if (!FactoryCategoryRegistry::isRegistered(Base::category())) {
      FactoryCategoryRegistry::registerCategory(Base::category(),
                                                new FactoryRegistryProxyImpl<Base>());
    }
  }

  /**
   * Constructor that registers an instance of the factory with the FactoryRegistry along with
   * deprecated names.
   */
  explicit RegisterFactory(std::initializer_list<absl::string_view> deprecated_names) {
    if (!instance_.name().empty()) {
      FactoryRegistry<Base>::registerFactory(instance_, instance_.name());
    } else {
      ASSERT(deprecated_names.size() != 0);
    }

    for (auto deprecated_name : deprecated_names) {
      ASSERT(!deprecated_name.empty());
      FactoryRegistry<Base>::registerFactory(instance_, deprecated_name, instance_.name());
    }

    if (!FactoryCategoryRegistry::isRegistered(Base::category())) {
      FactoryCategoryRegistry::registerCategory(Base::category(),
                                                new FactoryRegistryProxyImpl<Base>());
    }
  }

private:
  T instance_{};
};

/**
 * RegisterInternalFactory is a special case for registering factories
 * that are considered internal implementation details that should
 * not be exposed to operators via the factory categories.
 *
 * There is no corresponding REGISTER_INTERNAL_FACTORY because
 * this should be used sparingly and only in special cases.
 */
template <class T, class Base> class RegisterInternalFactory {
public:
  RegisterInternalFactory() {
    ASSERT(!instance_.name().empty());
    FactoryRegistry<Base>::registerFactory(instance_, instance_.name());
  }

private:
  T instance_{};
};

/**
 * Macro used for static registration.
 */
#define REGISTER_FACTORY(FACTORY, BASE)                                                            \
  ABSL_ATTRIBUTE_UNUSED void forceRegister##FACTORY() {}                                           \
  static Envoy::Registry::RegisterFactory</* NOLINT(fuchsia-statically-constructed-objects) */     \
                                          FACTORY, BASE>                                           \
      FACTORY##_registered

/**
 * Macro used for static registration declaration.
 * Calling forceRegister...(); can be used to force the static factory initializer to run in a
 * setting in which Envoy is bundled as a static archive. In this case, the static initializer is
 * not run until a function in the compilation unit is invoked. The force function can be invoked
 * from a static library wrapper.
 */
#define DECLARE_FACTORY(FACTORY) ABSL_ATTRIBUTE_UNUSED void forceRegister##FACTORY()

} // namespace Registry
} // namespace Envoy
