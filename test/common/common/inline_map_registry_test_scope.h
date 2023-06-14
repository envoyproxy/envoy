#pragma once

#include "envoy/common/inline_map_registry.h"

namespace Envoy {

template <size_t N> class InlineMapRegistryTestScope {
public:
  static absl::string_view name() {
    static const std::string name = "BenchmarkTestScope_KeySize_" + std::to_string(N);
    return name;
  }

  static std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>
  initializeRegistry() {
    std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle> handles;

    // Force the inline map registry to be initialized and be added to the registry manager.
    InlineMapRegistry<InlineMapRegistryTestScope>::scopeId();

    for (size_t i = 0; i < N; ++i) {
      std::string key = "key_" + std::to_string(i);
      handles.push_back(InlineMapRegistry<InlineMapRegistryTestScope>::registerInlineKey(key));
    }

    // Force the inline map registry to be finalized.
    InlineMapRegistryManager::registryInfos();

    return handles;
  }

  static std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>
  inlineHandles() {
    static const std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>
        inline_handles = initializeRegistry();
    return inline_handles;
  }
};

} // namespace Envoy
