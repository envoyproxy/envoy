// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include <set>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

namespace quiche {

namespace {

absl::flat_hash_map<std::string, Flag*> MakeFlagMap() {
  absl::flat_hash_map<std::string, Flag*> flags;

#define QUICHE_FLAG(type, flag, value, help) flags.emplace(FLAGS_##flag->name(), FLAGS_##flag);
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG

  return flags;
}

} // namespace

// static
FlagRegistry& FlagRegistry::GetInstance() {
  static auto* instance = new FlagRegistry();
  return *instance;
}

FlagRegistry::FlagRegistry() : flags_(MakeFlagMap()) {}

void FlagRegistry::ResetFlags() const {
  for (auto& kv : flags_) {
    kv.second->ResetValue();
  }
}

Flag* FlagRegistry::FindFlag(const std::string& name) const {
  auto it = flags_.find(name);
  return (it != flags_.end()) ? it->second : nullptr;
}

template <> bool TypedFlag<bool>::SetValueFromString(const std::string& value_str) {
  static const auto* kTrueValues = new std::set<std::string>({"1", "t", "true", "y", "yes"});
  static const auto* kFalseValues = new std::set<std::string>({"0", "f", "false", "n", "no"});
  auto lower = absl::AsciiStrToLower(value_str);
  if (kTrueValues->find(lower) != kTrueValues->end()) {
    SetValue(true);
    return true;
  }
  if (kFalseValues->find(lower) != kFalseValues->end()) {
    SetValue(false);
    return true;
  }
  return false;
}

template <> bool TypedFlag<int32_t>::SetValueFromString(const std::string& value_str) {
  int32_t value;
  if (absl::SimpleAtoi(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<int64_t>::SetValueFromString(const std::string& value_str) {
  int64_t value;
  if (absl::SimpleAtoi(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<double>::SetValueFromString(const std::string& value_str) {
  double value;
  if (absl::SimpleAtod(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<std::string>::SetValueFromString(const std::string& value_str) {
  SetValue(value_str);
  return true;
}

// Flag definitions
#define QUICHE_FLAG(type, flag, value, help)                                                       \
  TypedFlag<type>* FLAGS_##flag = new TypedFlag<type>(#flag, value, help);
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG

} // namespace quiche
