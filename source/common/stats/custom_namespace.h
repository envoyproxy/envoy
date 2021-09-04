#pragma once

#include "absl/container/flat_hash_set.h"

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
 */
class CustomStatNamespaces {
public:
  CustomStatNamespaces() = default;
  ~CustomStatNamespaces() = default;

  /**
   * @param name is the name to check.
   * @return true if the given name is registered as a custom stat namespace, false otherwise.
   */
  bool registered(const absl::string_view& name) const;

  /**
   * Used to register a custom namespace by extensions.
   * @param name is the name to register.
   */
  void registerStatNamespace(const absl::string_view& name);

  /**
   * Sanitizes the given stat name depending on whether or not it lives in a registered custom
   * stat namespace. If so, the custom stat namespace is trimmed from the input and
   * returns the trimmed string. Otherwise return null.
   * @param stat_name is the view to modify. If it is not in any custom registered namespaces, it
   * will never be modified.
   * @return the sanitized string if stat_name has a registered custom stat namespace. Otherwise,
   * return null.
   */
  absl::optional<std::string> trySanitizeStatName(const absl::string_view& stat_name) const;

private:
  absl::flat_hash_set<std::string> namespaces_;
};

/**
 * Returns the global mutable singleton of CustomStatNamespaces.
 * Stat sinks and extensions must use CustomStatNamespaces via this getter
 * except unit tests.
 */
CustomStatNamespaces& getCustomStatNamespaces();

} // namespace Stats
} // namespace Envoy
