#pragma once

#include <memory>
#include <string>

#include "envoy/formatter/substitution_formatter_base.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnFormatterConfigNewType = decltype(&envoy_dynamic_module_on_formatter_config_new);
using OnFormatterConfigDestroyType = decltype(&envoy_dynamic_module_on_formatter_config_destroy);
using OnFormatterParseType = decltype(&envoy_dynamic_module_on_formatter_parse);
using OnFormatterProviderDestroyType =
    decltype(&envoy_dynamic_module_on_formatter_provider_destroy);
using OnFormatterFormatType = decltype(&envoy_dynamic_module_on_formatter_format);

/**
 * Configuration for a dynamic module formatter. This resolves and holds the symbols used for
 * command parsing and value formatting along with the in-module command parser configuration. It is
 * shared by the command parser and every formatter provider it produces so that the module and its
 * configuration outlive all providers, even after the parser itself is destroyed.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleFormatterConfig() to provide graceful error handling. The constructor only
 * initializes basic members.
 */
class DynamicModuleFormatterConfig {
public:
  DynamicModuleFormatterConfig(absl::string_view formatter_name, absl::string_view formatter_config,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleFormatterConfig();

  // The corresponding in-module command parser configuration.
  envoy_dynamic_module_type_formatter_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. All are guaranteed non-nullptr after
  // newDynamicModuleFormatterConfig() succeeds.
  OnFormatterConfigDestroyType on_config_destroy_{nullptr};
  OnFormatterParseType on_parse_{nullptr};
  OnFormatterProviderDestroyType on_provider_destroy_{nullptr};
  OnFormatterFormatType on_format_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleFormatterConfig>>
  newDynamicModuleFormatterConfig(absl::string_view formatter_name,
                                  absl::string_view formatter_config,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string formatter_name_;
  const std::string formatter_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleFormatterConfigSharedPtr = std::shared_ptr<DynamicModuleFormatterConfig>;

/**
 * Creates a new DynamicModuleFormatterConfig for the given configuration.
 * @param formatter_name the name of the formatter.
 * @param formatter_config the configuration bytes for the formatter.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if symbol resolution or in-module
 * initialization failed.
 */
absl::StatusOr<DynamicModuleFormatterConfigSharedPtr>
newDynamicModuleFormatterConfig(absl::string_view formatter_name,
                                absl::string_view formatter_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * FormatterProvider that delegates value production for a single parsed command to the dynamic
 * module. Providers are invoked on worker threads and hold the configuration alive via a shared
 * pointer.
 */
class DynamicModuleFormatterProvider : public ::Envoy::Formatter::FormatterProvider {
public:
  DynamicModuleFormatterProvider(DynamicModuleFormatterConfigSharedPtr config,
                                 envoy_dynamic_module_type_formatter_provider_module_ptr provider);

  ~DynamicModuleFormatterProvider() override;

  // Formatter::FormatterProvider
  absl::optional<std::string> format(const ::Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const ::Envoy::Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  const DynamicModuleFormatterConfigSharedPtr config_;
  const envoy_dynamic_module_type_formatter_provider_module_ptr provider_;
};

/**
 * CommandParser that delegates command recognition to the dynamic module. Created by the factory at
 * configuration time and used by SubstitutionFormatParser::parse().
 */
class DynamicModuleCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  explicit DynamicModuleCommandParser(DynamicModuleFormatterConfigSharedPtr config);

  // Formatter::CommandParser
  ::Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                                 absl::string_view command_arg,
                                                 absl::optional<size_t> max_length) const override;

private:
  const DynamicModuleFormatterConfigSharedPtr config_;
};

/**
 * Per-format context passed to the module as the formatter_context_envoy_ptr. It bundles the
 * formatting context and stream info so the formatter callbacks can read request and response
 * state. Valid only for the duration of a single format call.
 */
struct FormatterContext {
  const ::Envoy::Formatter::Context* context;
  const StreamInfo::StreamInfo* stream_info;
};

} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
