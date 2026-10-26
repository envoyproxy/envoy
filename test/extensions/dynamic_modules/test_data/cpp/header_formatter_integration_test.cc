// Integration test module for HTTP/1 header formatter dynamic modules.
//
// The "preserve_case" configuration remembers every key the peer sent and restores that spelling
// on the way out, upper-casing keys it never saw so the test can tell the processKey path from the
// format path. The "counting" configuration exercises the requirement that one configuration
// object is shared by every worker thread, and "decline_formatter" never creates a formatter so
// the test can observe the fallback to Envoy's default casing. An unknown name is not registered
// at all, which makes Envoy reject the configuration.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk_header_formatter.h"

namespace Envoy {
namespace DynamicModules {

namespace {

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::string toUpper(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

} // namespace

// Per-message state: the keys this message's peer actually sent, indexed by their lower-cased
// form. Envoy calls every method from the one worker thread owning the message, so a plain map
// needs no synchronization.
class PreserveCaseFormatter : public HeaderFormatter {
public:
  PreserveCaseFormatter(std::string_view extra_key, HeaderFormatterHandle& handle)
      : extra_key_(extra_key), handle_(handle) {}

  void processKey(std::string_view key) override {
    // The handle is the module's window onto the host for this message. It is handed over with the
    // formatter and stays valid for as long as it lives, so it is kept as a member here. Only
    // logging is exposed today, and logging every key at trace level is how this module proves the
    // handle works.
    DYM_LOG(handle_, LogLevel::Trace, "header formatter observed key: {}", key);
    observed_[toLower(key)] = std::string(key);
  }

  std::optional<std::string_view> format(std::string_view key) override {
    // Exercises the third handle accessor: a module can align its own verbosity with Envoy's.
    if (handle_.getLogLevel() == LogLevel::Trace) {
      DYM_LOG(handle_, LogLevel::Trace, "header formatter formatting key: {}", key);
    }
    if (!extra_key_.empty() && toLower(key) == toLower(extra_key_)) {
      formatted_ = toUpper(key);
      return formatted_;
    }
    const auto it = observed_.find(std::string(key));
    // Never observed means the header was added by Envoy itself. Upper-casing it makes the two
    // paths distinguishable in the test.
    formatted_ = it != observed_.end() ? it->second : toUpper(key);
    return formatted_;
  }

private:
  const std::string extra_key_;
  HeaderFormatterHandle& handle_;
  std::map<std::string, std::string> observed_;
  // Holds the value returned by the last format call: the ABI requires it to stay valid until the
  // next call into the module.
  std::string formatted_;
};

// Shared by every worker thread, so it holds only immutable state.
class PreserveCaseConfig : public HeaderFormatterConfig {
public:
  explicit PreserveCaseConfig(std::string_view extra_key) : extra_key_(extra_key) {}

  std::unique_ptr<HeaderFormatter> create(HeaderFormatterHandle& handle) const override {
    return std::make_unique<PreserveCaseFormatter>(extra_key_, handle);
  }

private:
  // A key from the module configuration that is always upper-cased, even if the peer sent it in
  // another casing, which makes the configuration bytes observable in the response.
  const std::string extra_key_;
};

class CountingFormatter : public HeaderFormatter {
public:
  explicit CountingFormatter(uint64_t count) : count_(count) {}

  std::optional<std::string_view> format(std::string_view key) override {
    // Report the formatter's ordinal in a header key so the test can see how many were created.
    if (key != "x-formatter-count") {
      return std::nullopt;
    }
    formatted_ = std::format("x-formatter-count-{}", count_);
    return formatted_;
  }

private:
  const uint64_t count_;
  std::string formatted_;
};

// Exercises the shared-instance requirement: one object serves every worker thread, so the only
// mutable state it keeps is an atomic.
class CountingConfig : public HeaderFormatterConfig {
public:
  std::unique_ptr<HeaderFormatter> create(HeaderFormatterHandle&) const override {
    return std::make_unique<CountingFormatter>(formatters_.fetch_add(1) + 1);
  }

private:
  mutable std::atomic<uint64_t> formatters_{0};
};

// Never creates a formatter, which must leave Envoy using its default header casing rather than
// failing the message.
class DeclineConfig : public HeaderFormatterConfig {
public:
  std::unique_ptr<HeaderFormatter> create(HeaderFormatterHandle&) const override { return nullptr; }
};

class PreserveCaseConfigFactory : public HeaderFormatterConfigFactory {
public:
  std::unique_ptr<HeaderFormatterConfig> create(HeaderFormatterConfigHandle&,
                                                std::string_view config_view) override {
    return std::make_unique<PreserveCaseConfig>(config_view);
  }
};

class CountingConfigFactory : public HeaderFormatterConfigFactory {
public:
  std::unique_ptr<HeaderFormatterConfig> create(HeaderFormatterConfigHandle&,
                                                std::string_view) override {
    return std::make_unique<CountingConfig>();
  }
};

class DeclineConfigFactory : public HeaderFormatterConfigFactory {
public:
  std::unique_ptr<HeaderFormatterConfig> create(HeaderFormatterConfigHandle&,
                                                std::string_view) override {
    return std::make_unique<DeclineConfig>();
  }
};

REGISTER_HEADER_FORMATTER_CONFIG_FACTORY(PreserveCaseConfigFactory, "preserve_case");
REGISTER_HEADER_FORMATTER_CONFIG_FACTORY(CountingConfigFactory, "counting");
REGISTER_HEADER_FORMATTER_CONFIG_FACTORY(DeclineConfigFactory, "decline_formatter");

} // namespace DynamicModules
} // namespace Envoy
