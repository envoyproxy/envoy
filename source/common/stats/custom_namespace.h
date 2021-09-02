#pragma once

#include "envoy/common/pure.h"
#include "envoy/stats/scope.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

/**
 * The factory which manages custom stat namespaces. Custom stat namespaces are registered
 * by extensions that create user-defined metrics, and these metrics are all prefixed
 * by the namespace. For example, Wasm extension registers "wasmcustom" as a custom stat namespace,
 * and all the metrics created by user Wasm programs are prefixed by "wasmcustom." internally.
 * This is mainly for distinguishing these "custom metrics" defined outside Envoy codebase from
 * the native metrics defined by Envoy codebase, and this way stat sinks are able to determine
 * how to expose these two kinds of metrics.
 */
class CustomStatNamespaceFactory {
public:
  virtual ~CustomStatNamespaceFactory() = default;
  virtual absl::string_view name() PURE;

  /**
   * Used to register a custom namespace by extensions.
   * @param name is the name to register.
   */
  virtual void registerStatNamespace(const absl::string_view& name) PURE;

  /**
   * Sanitizes the given stat name depending on whether or not it lives in a registered custom
   * stat namespace. If so, the custom stat namespace is trimmed from the input and
   * returns the trimmed string. Otherwise return the empty string.
   * @param stat_name is the view to modify. If it is not in any custom registered namespaces, it
   * will never be modified.
   * @return the sanitized string if stat_name has a registered custom stat namespace. Otherwise,
   * return the empty string.
   */
  virtual std::string trySanitizeStatName(const absl::string_view& stat_name) const PURE;

  std::string category() { return "envoy.stats"; }
};

class CustomStatNamespaceFactoryImpl : public CustomStatNamespaceFactory {
public:
  CustomStatNamespaceFactoryImpl() = default;
  ~CustomStatNamespaceFactoryImpl() override = default;
  absl::string_view name() override { return "envoy.stats.custom_namespace"; };
  void registerStatNamespace(const absl::string_view& name) override;
  std::string trySanitizeStatName(const absl::string_view& stat_name) const override;

private:
  absl::flat_hash_set<std::string> namespaces_;
};

} // namespace Stats
} // namespace Envoy
