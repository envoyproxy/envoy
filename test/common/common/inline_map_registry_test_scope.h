#pragma once

#include "envoy/common/inline_map_registry.h"

namespace Envoy {

template <size_t N> class InlineMapRegistryTestScope {
public:
  static absl::string_view name() {
    CONSTRUCT_ON_FIRST_USE(std::string, "BenchmarkTestScope_KeySize_" + std::to_string(N));
  }

  static std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>
  initializeRegistry() {
    std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle> handles;

    // Force the inline map registry to be initialized and be added to the registry manager.
    InlineMapRegistry<InlineMapRegistryTestScope>::initialize();

    for (size_t i = 0; i < N; ++i) {
      std::string key = "key_" + std::to_string(i);
      handles.push_back(InlineMapRegistry<InlineMapRegistryTestScope>::registerInlineKey(key));
    }

    // Force the inline map registry to be finalized.
    InlineMapRegistryManager::registriesInfo();

    return handles;
  }

  static const std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>&
  inlineHandles() {
    CONSTRUCT_ON_FIRST_USE(
        std::vector<typename InlineMapRegistry<InlineMapRegistryTestScope>::InlineHandle>,
        initializeRegistry());
  }
};

} // namespace Envoy
