// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <set>

#include "source/common/common/assert.h"
#include "source/common/http/utility.h"

#include "absl/flags/flag.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "quiche_platform_impl/quiche_flags_impl.h"

namespace {

// Envoy uses different default values for some QUICHE flags. The following methods
// ensure that the absl::Flag objects are created with the correct values for
// these flags. This ensures that the absl::FlagSaver finds the correct values
// and avoid a race condition between the dynamic initialization of these flags
// and the FlagSaver in tests.
template <typename T> constexpr T maybeOverride(absl::string_view /*name*/, T val) { return val; }

template <> constexpr bool maybeOverride<bool>(absl::string_view name, bool val) {
  if (name == "quic_reloadable_flag_quic_disable_version_draft_29") {
    // Envoy only supports RFC-v1 in the long term, so disable IETF draft 29 implementation by
    // default.
    return true;
  }
  if (name == "quic_reloadable_flag_quic_default_to_bbr") {
    // This flag enables BBR, otherwise QUIC will use Cubic which is less performant.
    return true;
  }
  if (name == "quic_header_size_limit_includes_overhead") {
    // Do not include 32-byte per-entry overhead while counting header size.
    return false;
  }

  return val;
}

template <> constexpr int32_t maybeOverride<int32_t>(absl::string_view name, int32_t val) {
  if (name == "quic_buffered_data_threshold") {
    // Set send buffer twice of max flow control window to ensure that stream send
    // buffer always takes all the data.
    // The max amount of data buffered is the per-stream high watermark + the max
    // flow control window of upstream. The per-stream high watermark should be
    // smaller than max flow control window to make sure upper stream can be flow
    // control blocked early enough not to send more than the threshold allows.
    // TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible
    // for now. 512MB is way to large, but the actual bytes buffered should be bound by the
    // negotiated upstream flow control window.
    return 2 * ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE; // 512MB
  }
  return val;
}

} // namespace

// Flag definitions
#define QUIC_FLAG(flag, value) ABSL_FLAG(bool, envoy_##flag, maybeOverride(#flag, value), "");
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

#define DEFINE_PROTOCOL_FLAG_IMPL(type, flag, value, help)                                         \
  ABSL_FLAG(type, envoy_##flag, maybeOverride(#flag, value), help);

#define DEFINE_PROTOCOL_FLAG_SINGLE_VALUE(type, flag, value, doc)                                  \
  DEFINE_PROTOCOL_FLAG_IMPL(type, flag, value, doc)

#define DEFINE_PROTOCOL_FLAG_TWO_VALUES(type, flag, internal_value, external_value, doc)           \
  DEFINE_PROTOCOL_FLAG_IMPL(type, flag, external_value, doc)

// Select the right macro based on the number of arguments.
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6

#define PROTOCOL_FLAG_MACRO_CHOOSER(...)                                                           \
  GET_6TH_ARG(__VA_ARGS__, DEFINE_PROTOCOL_FLAG_TWO_VALUES, DEFINE_PROTOCOL_FLAG_SINGLE_VALUE)

#define QUIC_PROTOCOL_FLAG(...) PROTOCOL_FLAG_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define QUICHE_PROTOCOL_FLAG(...) PROTOCOL_FLAG_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG

#undef PROTOCOL_FLAG_MACRO_CHOOSER
#undef GET_6TH_ARG
#undef DEFINE_PROTOCOL_FLAG_TWO_VALUES
#undef DEFINE_PROTOCOL_FLAG_SINGLE_VALUE

namespace quiche {

namespace {

absl::flat_hash_map<absl::string_view, ReloadableFlag*> makeReloadableFlagMap() {
  absl::flat_hash_map<absl::string_view, ReloadableFlag*> flags;

  ASSERT(absl::GetFlag(FLAGS_envoy_quic_restart_flag_quic_testonly_default_true) == true);
#define QUIC_FLAG(flag, ...) flags.emplace("FLAGS_envoy_" #flag, &FLAGS_envoy_##flag);
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
  return flags;
}

} // namespace

FlagRegistry::FlagRegistry() : reloadable_flags_(makeReloadableFlagMap()) {}

// static
FlagRegistry& FlagRegistry::getInstance() {
  static auto* instance = new FlagRegistry();
  return *instance;
}

void FlagRegistry::updateReloadableFlags(
    const absl::flat_hash_map<std::string, bool>& quiche_flags_override) {
  for (auto& [flag_name, flag] : reloadable_flags_) {
    const auto it = quiche_flags_override.find(flag_name);
    if (it != quiche_flags_override.end()) {
      absl::SetFlag(flag, it->second);
    }
  }
}

} // namespace quiche
