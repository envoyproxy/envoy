#pragma once

#include "library/cc/bridge_utility.h"
#include "library/cc/direct_response_testing.h"
#include "library/cc/engine_builder.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/main_interface.h"
#include "library/common/stats/utility.h"

// This file exists in order to expose headers for Envoy's C++ libraries
// to Envoy Mobile's Swift implementation.
// Further, Swift only supports interacting with a subset of C++ language features
// so some types are renamed with the `using` keyword, and some features that
// cannot be imported into Swift are abstracted via interfaces that are supported.
// See this document on Swift's C++ interoperability status to learn more:
// https://github.com/apple/swift/blob/swift-5.7.3-RELEASE/docs/CppInteroperability/CppInteroperabilityStatus.md

namespace Envoy {
namespace CxxSwift {

using StringVector = std::vector<std::string>;
using StringPair = std::pair<std::string, std::string>;
using StringPairVector = std::vector<StringPair>;
using StringMap = absl::flat_hash_map<std::string, std::string>;
using HeaderMatcherVector = std::vector<DirectResponseTesting::HeaderMatcher>;
using BootstrapPtr = intptr_t;

// Exposes `map[std::move(key)] = std::move(value)` to Swift.
inline void string_map_set(StringMap& map, std::string key, std::string value) {
  map[std::move(key)] = std::move(value);
}

// Exposes `map[std::move(key)] = std::move(value)` to Swift.
inline void raw_header_map_set(Platform::RawHeaderMap& map, std::string key,
                               std::vector<std::string> value) {
  map[std::move(key)] = std::move(value);
}

// Smart pointers aren't currently supported by Swift / C++ interop, so we "erase"
// it into a `BootstrapPtr` / `intptr_t`, which we can import from Swift.
inline BootstrapPtr generateBootstrapPtr(Platform::EngineBuilder& builder) {
  return reinterpret_cast<BootstrapPtr>(builder.generateBootstrap().release());
}

inline std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>
bootstrapFromPtr(BootstrapPtr bootstrap_ptr) {
  return absl::WrapUnique(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
}

inline std::string bootstrapDebugDescription(BootstrapPtr bootstrap_ptr) {
  return bootstrapFromPtr(bootstrap_ptr)->ShortDebugString();
}

/**
 * Run the engine with the provided configuration.
 * @param bootstrap_ptr, the Envoy bootstrap configuration to use.
 * @param log_level, the log level.
 * @param engine_handle, the handle to an Envoy engine instance.
 */
void run(BootstrapPtr bootstrap_ptr, Platform::LogLevel log_level, envoy_engine_t engine_handle);

} // namespace CxxSwift
} // namespace Envoy
