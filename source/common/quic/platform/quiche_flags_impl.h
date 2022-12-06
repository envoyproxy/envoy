#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <atomic>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/synchronization/mutex.h"

// Not wired into command-line parsing.
#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, flag, value, help)                                \
  quiche::TypedFlag<type>* FLAGS_##flag = new TypedFlag<type>(#flag, value, help);

namespace quiche {

const std::string EnvoyQuicheReloadableFlagPrefix =
    "envoy.reloadable_features.FLAGS_envoy_quic_reloadable_flag_";
const std::string EnvoyFeaturePrefix = "envoy.reloadable_features.";

using ReloadableFlag = absl::Flag<bool>;

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

} // namespace quiche

// Flag declarations
#define QUIC_FLAG(flag, ...) ABSL_DECLARE_FLAG(bool, envoy_##flag);
#include "quiche/quic/core/quic_flags_list.h"
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_false, false)  // NOLINT
QUIC_FLAG(quic_reloadable_flag_spdy_testonly_default_true, true)    // NOLINT
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_false, false)     // NOLINT
QUIC_FLAG(quic_restart_flag_spdy_testonly_default_true, true)       // NOLINT
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_false, false) // NOLINT
QUIC_FLAG(quic_reloadable_flag_http2_testonly_default_true, true)   // NOLINT
QUIC_FLAG(quic_restart_flag_http2_testonly_default_false, false)    // NOLINT
QUIC_FLAG(quic_restart_flag_http2_testonly_default_true, true)      // NOLINT
#undef QUIC_FLAG

#define QUIC_PROTOCOL_FLAG(type, flag, ...) ABSL_DECLARE_FLAG(type, envoy_##flag);
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define QUICHE_PROTOCOL_FLAG(type, flag, ...) ABSL_DECLARE_FLAG(type, envoy_##flag);
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG

namespace quiche {

#define GetQuicheFlagImpl(flag) absl::GetFlag(FLAGS_envoy_##flag)

#define SetQuicheFlagImpl(flag, value) absl::SetFlag(&FLAGS_envoy_##flag, value)

#define GetQuicheReloadableFlagImpl(module, flag)                                                  \
  absl::GetFlag(FLAGS_envoy_quic_reloadable_flag_##flag)

#define SetQuicheReloadableFlagImpl(module, flag, value)                                           \
  absl::SetFlag(&FLAGS_envoy_quic_reloadable_flag_##flag, value)

#define GetQuicheRestartFlagImpl(module, flag) absl::GetFlag(FLAGS_envoy_quic_restart_flag_##flag)

#define SetQuicheRestartFlagImpl(module, flag, value)                                              \
  absl::SetFlag(&FLAGS_envoy_quic_restart_flag_##flag, value)

} // namespace quiche
