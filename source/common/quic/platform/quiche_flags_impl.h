#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <atomic>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace quiche {

const std::string EnvoyQuicheReloadableFlagPrefix =
    "envoy.reloadable_features.FLAGS_quic_reloadable_flag_";
const std::string EnvoyFeaturePrefix = "envoy.reloadable_features.";

class Flag;

// TODO: modify flags implementation to be backed by
// Runtime::runtimeFeatureEnabled(), which is the canonical Envoy way of
// enabling and disabling features.

// Registry of QUICHE flags. Can be used to reset all flags to default values,
// and to look up and set flags by name.
class FlagRegistry {
public:
  ~FlagRegistry() = default;

  // Return singleton instance.
  static FlagRegistry& getInstance();

  // Reset all registered flags to their default values.
  void resetFlags() const;

  // Look up a flag by name.
  Flag* findFlag(absl::string_view name) const;

  void updateReloadableFlags(const absl::flat_hash_map<std::string, bool>& quiche_flags_override);

private:
  FlagRegistry();

  const absl::flat_hash_map<absl::string_view, Flag*> flags_;
};

// Abstract class for QUICHE protocol and feature flags.
class Flag {
public:
  // Construct Flag with the given name and help string.
  Flag(const char* name, const char* help) : name_(name), help_(help) {}
  virtual ~Flag() = default;

  // Set flag value from given string, returning true iff successful.
  virtual bool setValueFromString(const std::string& value_str) = 0;

  // Reset flag to default value.
  virtual void resetValue() = 0;

  virtual void resetReloadedValue() = 0;

  // Return flag name.
  absl::string_view name() const { return name_; }

  // Return flag help string.
  absl::string_view help() const { return help_; }

private:
  std::string name_;
  std::string help_;
};

// Concrete class for QUICHE protocol and feature flags, templated by flag type.
template <typename T> class TypedFlag : public Flag {
public:
  TypedFlag(const char* name, T default_value, const char* help)
      : Flag(name, help), value_(default_value), explicit_value_(default_value), default_value_(default_value) {
    ASSERT(std::atomic_is_lock_free(&value_));
  }

  bool setValueFromString(const std::string& value_str) override;

  void resetValue() override {
    explicit_value_.store(default_value_, std::memory_order_relaxed);
    value_.store(default_value_, std::memory_order_relaxed);
  }

  // Set flag value.
  void setValue(T value) {
    explicit_value_.store(value, std::memory_order_relaxed);
    value_.store(value, std::memory_order_relaxed);
  }

  // Return flag value.
  T value() const {
    return value_;
  }

  void setReloadedValue(T value) {
    value_.store(value, std::memory_order_relaxed);
  }

  void resetReloadedValue() override {
    value_.store(explicit_value_, std::memory_order_relaxed);
  }

private:
  std::atomic<T> value_;
  std::atomic<T> explicit_value_;
  const T default_value_;
};

// SetValueFromString specializations
template <> bool TypedFlag<bool>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<int32_t>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<int64_t>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<double>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<unsigned long>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<unsigned long long>::setValueFromString(const std::string& value_str);

// Flag declarations
#define QUIC_FLAG(flag, ...) extern TypedFlag<bool>* flag;
#include "quiche/quic/core/quic_flags_list.h"
QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_true, true)
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
