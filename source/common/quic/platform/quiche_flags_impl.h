#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <atomic>
#include <string>

#include "source/common/quic/platform/quiche_flags_constants.h"

#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/synchronization/mutex.h"

// Not wired into command-line parsing.
#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, flag, value, help)                                \
  quiche::TypedFlag<type>* FLAGS_##flag = new TypedFlag<type>(#flag, value, help);

namespace quiche {

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
#define QUICHE_FLAG(type, flag, ...) ABSL_DECLARE_FLAG(type, envoy_##flag);
#include "quiche/common/quiche_feature_flags_list.h"
#undef QUICHE_FLAG

#define QUICHE_PROTOCOL_FLAG(type, flag, ...) ABSL_DECLARE_FLAG(type, envoy_##flag);
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG

namespace quiche {

#define GetQuicheFlagImpl(flag) absl::GetFlag(FLAGS_envoy_##flag)

#define SetQuicheFlagImpl(flag, value) absl::SetFlag(&FLAGS_envoy_##flag, value)

#define GetQuicheReloadableFlagImpl(flag) absl::GetFlag(FLAGS_envoy_quiche_reloadable_flag_##flag)

#define SetQuicheReloadableFlagImpl(flag, value)                                                   \
  absl::SetFlag(&FLAGS_envoy_quiche_reloadable_flag_##flag, value)

#define GetQuicheRestartFlagImpl(flag) absl::GetFlag(FLAGS_envoy_quiche_restart_flag_##flag)

#define SetQuicheRestartFlagImpl(flag, value)                                                      \
  absl::SetFlag(&FLAGS_envoy_quiche_restart_flag_##flag, value)

} // namespace quiche
