#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

/** Host interface exposed while a header formatter configuration is being created. */
class HeaderFormatterConfigHandle {
public:
  virtual ~HeaderFormatterConfigHandle();

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

/**
 * Host interface exposed to a header formatter for a single HTTP/1 message.
 *
 * A handle is created with the formatter and lives exactly as long as it does, so a formatter is
 * free to keep the reference it is handed by HeaderFormatterConfig::create.
 */
class HeaderFormatterHandle {
public:
  virtual ~HeaderFormatterHandle();

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Returns the current effective log level of Envoy's logger. */
  virtual LogLevel getLogLevel() = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

/**
 * Formatter for the header keys of a single HTTP/1 message.
 *
 * One formatter is created per message and every method is called by the single Envoy worker
 * thread that owns that message, so a formatter may keep mutable state without synchronization.
 * That state is what makes the extension "stateful": keys seen by processKey while decoding can be
 * replayed by format when encoding on the same connection.
 */
class HeaderFormatter {
public:
  virtual ~HeaderFormatter();

  /**
   * Called for each header key the codec receives, with the casing the peer used. Headers that
   * Envoy itself adds never reach this method; format is still called for them.
   *
   * `key` is only valid for the duration of this call, so copy it to keep it.
   */
  virtual void processKey(std::string_view key) {}

  /**
   * Called for each header key Envoy is about to serialize, in its internal lower-cased form.
   *
   * Returns the formatted key, or an empty optional to leave the key unchanged. A formatter that
   * only rewrites some keys should return an empty optional for the rest rather than echoing them
   * back. Envoy copies the returned view only after this call has returned, so it must not point
   * at a local: back it with a member of the formatter, or with thread-local storage owned by the
   * module.
   *
   * `key` is only valid for the duration of this call.
   */
  virtual std::optional<std::string_view> format(std::string_view key) = 0;
};

/**
 * Configuration that produces the formatter for each HTTP/1 message.
 *
 * A single instance is created once on the main thread and shared by all worker threads, so
 * create is const and implementations must be safe for concurrent use. Keep per-message state on
 * the returned HeaderFormatter, and guard any mutable fields with atomics or other
 * synchronization.
 */
class HeaderFormatterConfig {
public:
  virtual ~HeaderFormatterConfig();

  /**
   * Returns the formatter for one HTTP/1 message. Returning nullptr makes Envoy use its default
   * header casing for that message rather than failing it.
   *
   * `handle` belongs to the formatter being created and stays valid until that formatter is
   * destroyed, so the returned object may hold on to the reference.
   */
  virtual std::unique_ptr<HeaderFormatter> create(HeaderFormatterHandle& handle) const = 0;

  /**
   * Called when Envoy destroys the header formatter configuration, which happens once the listener
   * or cluster owning the protocol options that reference it is drained and removed. Every
   * formatter this configuration produced has already been destroyed by then.
   */
  virtual void onDestroy() {}
};

/** Factory interface that parses config and creates thread-safe header formatter configs. */
class HeaderFormatterConfigFactory {
public:
  virtual ~HeaderFormatterConfigFactory();

  /**
   * Parses config_view and returns the shared, thread-safe configuration used for every message.
   * Returning nullptr rejects the configuration.
   */
  virtual std::unique_ptr<HeaderFormatterConfig> create(HeaderFormatterConfigHandle& handle,
                                                        std::string_view config_view) = 0;
};

/** Unique pointer alias for header formatter config factories stored in the registry. */
using HeaderFormatterConfigFactoryPtr = std::unique_ptr<HeaderFormatterConfigFactory>;

/** Registry of statically registered header formatter config factories. */
class HeaderFormatterConfigFactoryRegistry {
public:
  /** Returns the registered header formatter config factories keyed by name. */
  static const std::map<std::string_view, HeaderFormatterConfigFactoryPtr>& getRegistry();

private:
  static std::map<std::string_view, HeaderFormatterConfigFactoryPtr>& getMutableRegistry();
  friend class HeaderFormatterConfigFactoryRegister;
};

/** RAII helper that inserts and removes a header formatter config factory registration. */
class HeaderFormatterConfigFactoryRegister {
public:
  /** Registers a header formatter config factory under name for the binary lifetime. */
  HeaderFormatterConfigFactoryRegister(std::string_view name,
                                       HeaderFormatterConfigFactoryPtr factory);
  ~HeaderFormatterConfigFactoryRegister();

private:
  const std::string name_;
};

/** Registers a header formatter config factory during static initialization. */
#define REGISTER_HEADER_FORMATTER_CONFIG_FACTORY(FACTORY_CLASS, NAME)                              \
  static Envoy::DynamicModules::HeaderFormatterConfigFactoryRegister                               \
      HeaderFormatterConfigFactoryRegister_##FACTORY_CLASS##_register_NAME(                        \
          NAME, std::unique_ptr<Envoy::DynamicModules::HeaderFormatterConfigFactory>(              \
                    new FACTORY_CLASS()));

} // namespace DynamicModules
} // namespace Envoy
