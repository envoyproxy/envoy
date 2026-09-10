#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

/**
 * Host interface exposed to an early header mutation for a single request.
 *
 * A handle is valid only for the duration of one EarlyHeaderMutation::mutate call and must not be
 * stored. Because mutate runs concurrently on worker threads against one shared mutation object,
 * the handle is the only per-request state and is passed in as an argument rather than captured at
 * construction time.
 */
class EarlyHeaderMutationHandle {
public:
  virtual ~EarlyHeaderMutationHandle();

  /** Returns the mutable request headers for this request. */
  virtual HeaderMap& requestHeaders() = 0;

  /** Returns a string attribute value if one is present. */
  virtual std::optional<std::string_view> getAttributeString(AttributeID id) = 0;

  /** Returns an integer attribute value if one is present. */
  virtual std::optional<uint64_t> getAttributeInt(AttributeID id) = 0;

  /** Returns a boolean attribute value if one is present. */
  virtual std::optional<bool> getAttributeBool(AttributeID id) = 0;

  /** Returns a string dynamic metadata value if one is present. */
  virtual std::optional<std::string_view> getDynamicMetadataString(std::string_view filter_name,
                                                                   std::string_view path) = 0;

  /** Returns a numeric dynamic metadata value if one is present. */
  virtual std::optional<double> getDynamicMetadataNumber(std::string_view filter_name,
                                                         std::string_view path) = 0;

  /** Returns a boolean dynamic metadata value if one is present. */
  virtual std::optional<bool> getDynamicMetadataBool(std::string_view filter_name,
                                                     std::string_view path) = 0;

  /** Returns a raw bytes filter state value if one is present. */
  virtual std::optional<std::string_view> getFilterState(std::string_view key) = 0;

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

/** Host interface exposed while a thread-safe early header mutation is being created. */
class EarlyHeaderMutationConfigHandle {
public:
  virtual ~EarlyHeaderMutationConfigHandle();

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

/**
 * Base class for early header mutations, which rewrite request headers before routing, tracing,
 * request ID generation and any filter processing.
 *
 * A single instance is created once on the main thread and shared by all worker threads, so mutate
 * is const and implementations must be safe for concurrent use. Keep per-request state on the
 * handle, and guard any mutable fields with atomics or other synchronization.
 */
class EarlyHeaderMutation {
public:
  virtual ~EarlyHeaderMutation();

  /**
   * Rewrites the request headers for one request. Called concurrently on worker threads.
   *
   * The return value is NOT a success or failure indication. Returning true lets Envoy continue to
   * the next early header mutation extension in the configured chain; returning false stops the
   * chain so no later extension runs. Mutations already applied are kept either way. When this is
   * the last or only extension in the chain, the return value has no effect.
   *
   * `headers` is the same object as `handle.requestHeaders()`, passed separately for convenience.
   * Neither may be retained beyond this call.
   */
  virtual bool mutate(HeaderMap& headers, EarlyHeaderMutationHandle& handle) const = 0;

  /** Called when the early header mutation is being destroyed. */
  virtual void onDestroy() {}
};

/** Factory interface that parses config and creates thread-safe early header mutations. */
class EarlyHeaderMutationConfigFactory {
public:
  virtual ~EarlyHeaderMutationConfigFactory();

  /**
   * Parses config_view and returns the shared, thread-safe mutation used for every request.
   * Returning nullptr rejects the configuration.
   */
  virtual std::unique_ptr<EarlyHeaderMutation> create(EarlyHeaderMutationConfigHandle& handle,
                                                      std::string_view config_view) = 0;
};

/** Unique pointer alias for early header mutation config factories stored in the registry. */
using EarlyHeaderMutationConfigFactoryPtr = std::unique_ptr<EarlyHeaderMutationConfigFactory>;

/** Registry of statically registered early header mutation config factories. */
class EarlyHeaderMutationConfigFactoryRegistry {
public:
  /** Returns the registered early header mutation config factories keyed by name. */
  static const std::map<std::string_view, EarlyHeaderMutationConfigFactoryPtr>& getRegistry();

private:
  static std::map<std::string_view, EarlyHeaderMutationConfigFactoryPtr>& getMutableRegistry();
  friend class EarlyHeaderMutationConfigFactoryRegister;
};

/** RAII helper that inserts and removes an early header mutation config factory registration. */
class EarlyHeaderMutationConfigFactoryRegister {
public:
  /** Registers an early header mutation config factory under name for the binary lifetime. */
  EarlyHeaderMutationConfigFactoryRegister(std::string_view name,
                                           EarlyHeaderMutationConfigFactoryPtr factory);
  ~EarlyHeaderMutationConfigFactoryRegister();

private:
  const std::string name_;
};

/** Registers an early header mutation config factory during static initialization. */
#define REGISTER_EARLY_HEADER_MUTATION_CONFIG_FACTORY(FACTORY_CLASS, NAME)                         \
  static Envoy::DynamicModules::EarlyHeaderMutationConfigFactoryRegister                           \
      EarlyHeaderMutationConfigFactoryRegister_##FACTORY_CLASS##_register_NAME(                    \
          NAME, std::unique_ptr<Envoy::DynamicModules::EarlyHeaderMutationConfigFactory>(          \
                    new FACTORY_CLASS()));

} // namespace DynamicModules
} // namespace Envoy
