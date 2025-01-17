#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

/**
 * CustomStatNamespaces manages custom stat namespaces. Custom stat namespaces are registered
 * by extensions that create user-defined metrics, and these metrics are all prefixed
 * by the namespace. For example, Wasm extension registers "wasmcustom" as a custom stat namespace,
 * and all the metrics created by user Wasm programs are prefixed by "wasmcustom." internally.
 * This is mainly for distinguishing these "custom metrics" defined outside Envoy codebase from
 * the native metrics defined by Envoy codebase, and this way stat sinks are able to determine
 * how to expose these two kinds of metrics.
 * Note that the implementation will not be thread-safe so users of this class must be in the main
 * thread.
 */
class CustomStatNamespaces {
public:
  virtual ~CustomStatNamespaces() = default;

  /**
   * @param name is the name to check.
   * @return true if the given name is registered as a custom stat namespace, false otherwise.
   */
  virtual bool registered(const absl::string_view name) const PURE;

  /**
   * Used to register a custom namespace by extensions.
   * @param name is the name to register.
   */
  virtual void registerStatNamespace(const absl::string_view name) PURE;

  /**
   * Strips the registered custom stat namespace from the given stat name's prefix if it lives in a
   * registered custom stat namespace, and the stripped string is returned. Otherwise return
   * nullopt.
   * @param stat_name is the view to modify. If it is not in any custom registered namespaces, it
   * will never be modified.
   * @return the stripped string if stat_name has a registered custom stat namespace. Otherwise,
   * return nullopt.
   */
  virtual absl::optional<absl::string_view>
  stripRegisteredPrefix(const absl::string_view stat_name) const PURE;
};

} // namespace Stats
} // namespace Envoy
