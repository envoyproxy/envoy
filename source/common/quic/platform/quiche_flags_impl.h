#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <atomic>
#include <string>

#include "absl/container/flat_hash_map.h"

namespace quiche {

const std::string EnvoyQuicheReloadableFlagPrefix =
    "envoy.reloadable_features.FLAGS_quic_reloadable_flag_";
const std::string EnvoyFeaturePrefix = "envoy.reloadable_features.";

// Concrete class for QUICHE protocol and feature flags, templated by flag type.
template <typename T> class TypedFlag {
public:
  explicit TypedFlag(T value) : value_(value) {}

  // Set flag value.
  void setValue(T value) { value_.store(value, std::memory_order_relaxed); }

  // Return flag value.
  T value() const { return value_; }

private:
  std::atomic<T> value_; // Current value of the flag.
};

using ReloadableFlag = TypedFlag<bool>;

// Registry of QUICHE flags. Can be used to update reloadable flag values.
class FlagRegistry {
public:
  ~FlagRegistry() = default;

  // Return singleton instance.
  static FlagRegistry& getInstance();

  void updateReloadableFlags(const absl::flat_hash_map<std::string, bool>& quiche_flags_override);

private:
  FlagRegistry();

  const absl::flat_hash_map<absl::string_view, ReloadableFlag*> reloadable_flags_;
};

// Flag declarations
#define QUIC_FLAG(flag, ...) extern ReloadableFlag* FLAGS_##flag;
#include "quiche/quic/core/quic_flags_list.h"
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_false, false)
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_true, true)
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_false, false)
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_true, true)
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_false, false)
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_true, true)
QUIC_FLAG(quic_restart_flag_http2_testonly_default_false, false)
QUIC_FLAG(quic_restart_flag_http2_testonly_default_true, true)
#undef QUIC_FLAG

#define QUIC_PROTOCOL_FLAG(type, flag, ...) extern TypedFlag<type>* FLAGS_##flag;
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

// |flag| is the global flag variable, which is a pointer to TypedFlag<type>.
#define GetQuicheFlagImpl(flag) (quiche::flag)->value()

// |flag| is the global flag variable, which is a pointer to TypedFlag<type>.
#define SetQuicheFlagImpl(flag, value) (quiche::flag)->setValue(value)

#define GetQuicheReloadableFlagImpl(module, flag) quiche::FLAGS_quic_reloadable_flag_##flag->value()

#define SetQuicheReloadableFlagImpl(module, flag, value)                                           \
  quiche::FLAGS_quic_reloadable_flag_##flag->setValue(value)

#define GetQuicheRestartFlagImpl(module, flag) quiche::FLAGS_quic_restart_flag_##flag->value()

#define SetQuicheRestartFlagImpl(module, flag, value)                                              \
  quiche::FLAGS_quic_restart_flag_##flag->setValue(value)

} // namespace quiche
