#pragma once

#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/registry/registry.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace Envoy {
namespace Registry {

/**
 * Helper class to call `registeredNames` for a given type.
 */
class BaseExtensionFactoryRegistry {
public:
  virtual ~BaseExtensionFactoryRegistry() = default;
  virtual std::vector<absl::string_view> registeredNames() const PURE;
};

template <class Base> class ExtensionFactoryRegistry : public BaseExtensionFactoryRegistry {
public:
  using FactoryRegistry = Envoy::Registry::FactoryRegistry<Base>;

  std::vector<absl::string_view> registeredNames() const {
    return FactoryRegistry::registeredNames();
  }
};

/**
 * This is a registry of extension factory registries. The distinction between
 * a factory registry and an extension factory registry is that an extension
 * factory registry registers factories that are visible to user configuration
 * through the build system and API.
 */
class ExtensionFactoryRegistryRegistry {
public:
  using MapType = absl::flat_hash_map<absl::string_view, BaseExtensionFactoryRegistry*>;

  static MapType& factories() {
    static auto* factories = new MapType();
    return *factories;
  }

  static void registerExtensionFactoryRegistry(BaseExtensionFactoryRegistry* factory,
                                               absl::string_view category) {
    auto result = factories().emplace(std::make_pair(category, factory));
    if (!result.second) {
      throw EnvoyException(
          absl::Substitute("Double registration for extension category: '$0'", category));
    }
  }
};

template <class Base> class RegisterExtensionFactoryRegistry {
public:
  explicit RegisterExtensionFactoryRegistry(absl::string_view category) {
    ExtensionFactoryRegistryRegistry::registerExtensionFactoryRegistry(
        new ExtensionFactoryRegistry<Base>(), category);
  }
};

#define __EXT_CONCAT(a, b) a##b
#define _EXT_CONCAT(a, b) __EXT_CONCAT(a, b)

/**
 * Register an extension factory with the extensions registry. This
 * registry is used to list extensions by category. When adding a new
 * extension factory, register it once with the extensions registry
 * so that the extension factories will be automatically published.
 *
 * @param BASE is the factory class
 * @param CATEGORY is a string literal that names the extension type
 *
 * Example:
 *      // Register some factories.
 *      REGISTER_FACTORY(LicoriceFactory, CandyFactory);
 *      REGISTER_FACTORY(NougayFactory, CandyFactory);
 *
 *      // Register the factory registry.
 *      REGISTER_EXTENSION_FACTORY(CandyFactory, "candies");
 */
#define REGISTER_EXTENSION_FACTORY(BASE, CATEGORY)                                                 \
  static Envoy::Registry::RegisterExtensionFactoryRegistry<BASE> _EXT_CONCAT(                      \
      _ext_registered_, __COUNTER__)(CATEGORY)

} // namespace Registry
} // namespace Envoy
