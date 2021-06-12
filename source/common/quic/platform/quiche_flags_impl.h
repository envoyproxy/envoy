#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

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
      : Flag(name, help), value_(default_value), default_value_(default_value) {}

  bool setValueFromString(const std::string& value_str) override;

  void resetValue() override {
    absl::MutexLock lock(&mutex_);
    value_ = default_value_;
  }

  // Set flag value.
  void setValue(T value) {
    absl::MutexLock lock(&mutex_);
    value_ = value;
  }

  // Return flag value.
  T value() const {
    absl::MutexLock lock(&mutex_);
    if (has_reloaded_value_) {
      return reloaded_value_;
    }
    return value_;
  }

  void setReloadedValue(T value) {
    absl::MutexLock lock(&mutex_);
    has_reloaded_value_ = true;
    reloaded_value_ = value;
  }

  void resetReloadedValue() override {
    absl::MutexLock lock(&mutex_);
    has_reloaded_value_ = false;
  }

private:
  mutable absl::Mutex mutex_;
  T value_ ABSL_GUARDED_BY(mutex_);
  const T default_value_;
  bool has_reloaded_value_ ABSL_GUARDED_BY(mutex_) = false;
  T reloaded_value_ ABSL_GUARDED_BY(mutex_);
};

// SetValueFromString specializations
template <> bool TypedFlag<bool>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<int32_t>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<int64_t>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<double>::setValueFromString(const std::string& value_str);
template <> bool TypedFlag<std::string>::setValueFromString(const std::string& value_str);
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
