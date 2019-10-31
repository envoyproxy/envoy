#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/registry/registry.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Registry {

/**
 * Users of the ExtensionFactoryRegistryRegistry class must specialize
 * this class to define a category name for their extension.
 */
template <class Base> class ExtensionFactoryRegistryCategory {
public:
  static std::string category();
};

/**
 * Helper class to call `allFactoryNames` for a given type.
 */
class BaseExtensionFactoryRegistry {
public:
  virtual ~BaseExtensionFactoryRegistry() = default;
  virtual std::string factoryNames() const PURE;
};

template <class Base> class ExtensionFactoryRegistry : public BaseExtensionFactoryRegistry {
public:
  using FactoryRegistry = Envoy::Registry::FactoryRegistry<Base>;

  std::string factoryNames() const { return FactoryRegistry::allFactoryNames(); }
};

/**
 * This is a registry of extension factory registries. The distinction between
 * a factory registry and an extension factory registry is that an extension
 * factory registry registers factories that are visible to user configuration
 * through the build system and API.
 */
class ExtensionFactoryRegistryRegistry {
public:
  using MapType = absl::flat_hash_map<std::string, BaseExtensionFactoryRegistry*>;

  static MapType& factories() {
    static auto* factories = new MapType();
    return *factories;
  }

  static void registerExtensionFactoryRegistry(BaseExtensionFactoryRegistry* factory,
                                               absl::string_view name) {
    auto result = factories().emplace(std::make_pair(name, factory));
    if (!result.second) {
      throw EnvoyException(fmt::format("Double registration for extension category: '{}'", name));
    }
  }
};

template <class Base> class RegisterExtensionFactoryRegistry {
public:
  RegisterExtensionFactoryRegistry() {
    ExtensionFactoryRegistryRegistry::registerExtensionFactoryRegistry(
        new ExtensionFactoryRegistry<Base>(), ExtensionFactoryRegistryCategory<Base>::category());
  }
};

/**
 * This needs to be used at global scope (i.e. not within a namespace).
 */
#define REGISTER_EXTENSION_FACTORY(BASE, CATEGORY)                                                 \
  template <> std::string Envoy::Registry::ExtensionFactoryRegistryCategory<BASE>::category() {    \
    return CATEGORY;                                                                               \
  }                                                                                                \
  static Envoy::Registry::RegisterExtensionFactoryRegistry<BASE> _extension

} // namespace Registry
} // namespace Envoy
