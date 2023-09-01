#include "source/common/common/inline_map_registry.h"

namespace Envoy {

InlineMapRegistry::InlineMapDescriptor*
InlineMapRegistry::createDescriptor(absl::string_view scope_name) {
  // This should never be triggered because the developer should ensure all used/managed
  // descriptors are registered before main() function by static variable initialization.
  RELEASE_ASSERT(!finalized(), "Registry is finalized, cannot create new descriptor");

  // Create the descriptor and insert it into the registry.
  auto descriptor = new InlineMapDescriptor();
  auto [_, inserted] = registry().emplace(scope_name, descriptor);

  // Check whether the descriptor is created repeatedly.
  RELEASE_ASSERT(inserted, fmt::format("Create descriptor {} repeatedly", scope_name));

  return descriptor;
}

InlineMapRegistry::RegistryInfo InlineMapRegistry::registryInfo() {
  std::vector<std::pair<std::string, std::string>> registry_info;
  for (const auto& [name, descriptor] : registry()) {
    registry_info.emplace_back(name, descriptor->inlineKeysAsString());
  }
  return registry_info;
}

void InlineMapRegistry::finalize(const ScopeInlineKeysVector& scope_inline_keys_vector) {
  // Call once to finalize the registry and all managed descriptors. This is used to ensure
  // the registry is finalized only once on multi-thread environment.
  std::call_once(onceFlag(), [&scope_inline_keys_vector]() {
    finalized() = true;

    // Add inline keys to the descriptor based on the scope name. This is last chance to add
    // inline keys to the descriptor.
    for (const auto& inline_keys : scope_inline_keys_vector) {
      auto iter = registry().find(inline_keys.name);
      if (iter == registry().end()) {
        // Skip the scope that is not registered.
        ENVOY_LOG_MISC(warn, "Skip the scope {} that is not registered", inline_keys.name);
        continue;
      }

      for (const auto& key : inline_keys.keys) {
        iter->second->addInlineKey(key);
      }
    }

    // Finalize all managed descriptors.
    std::for_each(registry().begin(), registry().end(), [](auto& p) { p.second->finalize(); });
  });
}

} // namespace Envoy
