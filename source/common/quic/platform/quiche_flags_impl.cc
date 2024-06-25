// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <set>
#include <string>
#include <variant>
#include <vector>

#include "source/common/common/assert.h"

#include "absl/flags/flag.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "quiche_platform_impl/quiche_flags_impl.h"

namespace {

#define QUICHE_RELOADABLE_FLAG_OVERRIDE(flag_name, value)                                          \
  {STRINGIFY(quic_reloadable_flag_##flag_name), value},
constexpr std::pair<absl::string_view, bool> quiche_reloadable_flag_overrides[]{
    OVERRIDDEN_RELOADABLE_FLAGS(QUICHE_RELOADABLE_FLAG_OVERRIDE)};
#undef QUICHE_RELOADABLE_FLAG_OVERRIDE

#define QUICHE_PROTOCOL_FLAG_OVERRIDE(flag_name, value) {STRINGIFY(flag_name), value},
constexpr std::pair<absl::string_view, absl::variant<bool, uint32_t>>
    quiche_protocol_flag_overrides[]{OVERRIDDEN_PROTOCOL_FLAGS(QUICHE_PROTOCOL_FLAG_OVERRIDE)};
#undef QUICHE_PROTOCOL_FLAG_OVERRIDE

// Envoy uses different default values for some QUICHE flags. The following methods
// ensure that the absl::Flag objects are created with the correct values for
// these flags. This ensures that the absl::FlagSaver finds the correct values
// and avoid a race condition between the dynamic initialization of these flags
// and the FlagSaver in tests.
template <typename T> constexpr T maybeOverride(absl::string_view /*name*/, T val) { return val; }

template <> constexpr bool maybeOverride<bool>(absl::string_view name, bool val) {
  for (const auto& [flag_name, new_value] : quiche_reloadable_flag_overrides) {
    if (flag_name == name) {
      return new_value;
    }
  }
  for (const auto& [flag_name, new_value_variant] : quiche_protocol_flag_overrides) {
    if (flag_name == name) {
      if (absl::holds_alternative<bool>(new_value_variant)) {
        return absl::get<bool>(new_value_variant);
      }
    }
  }
  return val;
}

template <> constexpr int32_t maybeOverride<int32_t>(absl::string_view name, int32_t val) {
  for (const auto& [flag_name, new_value_variant] : quiche_protocol_flag_overrides) {
    if (flag_name == name) {
      if (absl::holds_alternative<uint32_t>(new_value_variant)) {
        return absl::get<uint32_t>(new_value_variant);
      }
    }
  }
  return val;
}

} // namespace

// Flag definitions
#define QUIC_FLAG(flag, value) ABSL_FLAG(bool, envoy_##flag, maybeOverride(#flag, value), "");
#include "quiche/quic/core/quic_flags_list.h"
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
#undef QUIC_FLAG
  return flags;
}

} // namespace

FlagRegistry::FlagRegistry() : reloadable_flags_(makeReloadableFlagMap()) {}

// static
FlagRegistry& FlagRegistry::getInstance() {
  static auto* instance = new FlagRegistry();
  ASSERT(sizeof(quiche_reloadable_flag_overrides) / sizeof(std::pair<absl::string_view, bool>) ==
         3);
  ASSERT(sizeof(quiche_protocol_flag_overrides) /
             sizeof(std::pair<absl::string_view, absl::variant<bool, uint32_t>>) ==
         3);
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
