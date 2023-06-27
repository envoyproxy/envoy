#include "source/common/common/inline_map_registry.h"

namespace Envoy {

/**
 * Helper class to get inline map registry singleton based on the scope. If cross-modules inline map
 * registry is necessary, this class should be used.
 */
class InlineMapRegistryHelper {
public:
  using InlineKey = InlineMapRegistry<std::string>::InlineKey;
  using ManagedRegistries = absl::flat_hash_map<std::string, InlineMapRegistry<std::string>*>;

  /**
   * Get the inline map registry singleton based on the scope.
   */
  template <class Scope>
  static InlineMapRegistry<std::string>::InlineKey registerInlinKey(absl::string_view key) {
    return mutableRegistry<Scope>().registerInlineKey(key);
  }

  /**
   * Get registered inline key by the key name.
   */
  template <class Scope>
  static absl::optional<InlineMapRegistry<std::string>::InlineKey>
  getInlineKey(absl::string_view key) {
    return mutableRegistry<Scope>().getInlineKey(key);
  }

  /**
   * Create inline map based on the scope.
   */
  template <class Scope, class Value> static auto createInlineMap() {
    return InlineMap<std::string, Value>::create(mutableRegistry<Scope>());
  }

  /**
   * Get all registries managed by this helper. This this helpful to finalize all registries and
   * print the debug info of inline map registry.
   */
  static const ManagedRegistries& registries() { return mutableRegistries(); }

  /**
   * Finalize all registries managed by this helper. This should be called after all inline keys
   * are registered.
   */
  static void finalize() {
    for (auto& registry : mutableRegistries()) {
      registry.second->finalize();
    }
  }

private:
  template <class Scope> static InlineMapRegistry<std::string>& mutableRegistry() {
    static InlineMapRegistry<std::string>* registry = []() {
      auto* registry = new InlineMapRegistry<std::string>();
      auto result = InlineMapRegistryHelper::mutableRegistries().emplace(Scope::name(), registry);
      RELEASE_ASSERT(result.second,
                     absl::StrCat("Duplicate inline map registry for scope: ", Scope::name()));
      return registry;
    }();
    return *registry;
  }

  static ManagedRegistries& mutableRegistries() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(ManagedRegistries);
  }
};

} // namespace Envoy
