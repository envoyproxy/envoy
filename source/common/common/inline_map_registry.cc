#include "source/common/common/inline_map_registry.h"

namespace Envoy {

InlineMapRegistry::Descriptor* InlineMapRegistry::createDescriptor(absl::string_view scope_name) {
  // This should never be triggered because the developer should ensure all used/managed
  // descriptors are registered before main() function by static variable initialization.
  RELEASE_ASSERT(!finalized_, "Registry is finalized, cannot create new descriptor");

  // Create the descriptor and insert it into the registry.
  auto descriptor = new Descriptor();
  auto [_, inserted] = descriptors_.emplace(scope_name, descriptor);

  // Check whether the descriptor is created repeatedly.
  RELEASE_ASSERT(inserted, fmt::format("Create descriptor {} repeatedly", scope_name));

  return descriptor;
}

const InlineMapRegistry::Descriptors& InlineMapRegistry::descriptors() const {
  RELEASE_ASSERT(finalized_, "Registry is not finalized, cannot get registry");
  return descriptors_;
}

void InlineMapRegistry::finalize(const ScopeInlineKeysVector& scope_inline_keys_vector) {
  // Call once to finalize the registry and all managed descriptors. This is used to ensure
  // the registry is finalized only once on multi-thread environment.
  std::call_once(once_flag_, [this, &scope_inline_keys_vector]() {
    finalized_ = true;

    // Add inline keys to the descriptor based on the scope name. This is last chance to add
    // inline keys to the descriptor.
    for (const auto& inline_keys : scope_inline_keys_vector) {
      auto iter = descriptors_.find(inline_keys.name);
      if (iter == descriptors_.end()) {
        // Skip the scope that is not registered.
        ENVOY_LOG_MISC(warn, "Skip the scope {} that is not registered", inline_keys.name);
        continue;
      }

      for (const auto& key : inline_keys.keys) {
        iter->second->addInlineKey(key);
      }
    }

    // Finalize all managed descriptors.
    std::for_each(descriptors_.begin(), descriptors_.end(), [](auto& p) { p.second->finalize(); });
  });
}

} // namespace Envoy
