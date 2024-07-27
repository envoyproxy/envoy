#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Formatter {

/**
 * Template interface for multiple protocols/modules formatters.
 */
template <class FormatterContext> class FormatterBase {
public:
  virtual ~FormatterBase() = default;

  /**
   * Return a formatted substitution line.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string formatWithContext(const FormatterContext& context,
                                        const StreamInfo::StreamInfo& stream_info) const PURE;
};

template <class FormatterContext>
using FormatterBasePtr = std::unique_ptr<FormatterBase<FormatterContext>>;

/**
 * Template interface for multiple protocols/modules formatter providers.
 */
template <class FormatterContext> class FormatterProviderBase {
public:
  virtual ~FormatterProviderBase() = default;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual absl::optional<std::string>
  formatWithContext(const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         context and stream info.
   */
  virtual ProtobufWkt::Value
  formatValueWithContext(const FormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const PURE;
};

template <> class FormatterProviderBase<void> {
public:
  virtual ~FormatterProviderBase() = default;

  /**
   * Format the value with the given stream info.
   * @param stream_info supplies the stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given stream info.
   */
  virtual absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Format the value with the given stream info.
   * @param stream_info supplies the stream info.
   * @return ProtobufWkt::Value containing a single value extracted from the given stream info.
   */
  virtual ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const PURE;
};

template <class FormatterContext>
using FormatterProviderBasePtr = std::unique_ptr<FormatterProviderBase<FormatterContext>>;
using StreamInfoFormatterProvider = FormatterProviderBase<void>;
using StreamInfoFormatterProviderPtr = FormatterProviderBasePtr<void>;

template <class FormatterContext> class CommandParserBase {
public:
  virtual ~CommandParserBase() = default;

  /**
   * Return a FormatterProviderBasePtr if command arg and max_length are correct for the formatter
   * provider associated with command.
   * @param command command name.
   * @param command_arg command specific argument. Empty if no argument is provided.
   * @param max_length length to which the output produced by FormatterProvider
   *                   should be truncated to (optional).
   *
   * @return FormattterProviderPtr substitution provider for the parsed command.
   */
  virtual FormatterProviderBasePtr<FormatterContext>
  parse(const std::string& command, const std::string& command_arg,
        absl::optional<size_t>& max_length) const PURE;
};

template <class FormatterContext>
using CommandParserBasePtr = std::unique_ptr<CommandParserBase<FormatterContext>>;
using StreamInfoCommandParser = CommandParserBase<void>;
using StreamInfoCommandParserPtr = CommandParserBasePtr<void>;

template <class FormatterContext> class CommandParserFactoryBase : public Config::TypedFactory {
public:
  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserBasePtr<FormatterContext>
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.formatter"; }
};

template <class FormatterContext>
class BuiltInCommandParserFactoryBase : public Config::UntypedFactory {
public:
  std::string category() const override { return "envoy.built_in_formatters"; }

  /**
   * Creates a particular CommandParser implementation.
   */
  virtual CommandParserBasePtr<FormatterContext> createCommandParser() const PURE;
};

using BuiltInStreamInfoCommandParserFactory = BuiltInCommandParserFactoryBase<void>;

/**
 * Helper class to get all built-in command parsers for a given formatter context.
 */
template <class FormatterContext> class BuiltInCommandParserFactoryHelper {
public:
  using Factory = BuiltInCommandParserFactoryBase<FormatterContext>;
  using Parsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  /**
   * Get all built-in command parsers for a given formatter context.
   * @return Parsers all built-in command parsers for a given formatter context.
   */
  static const Parsers& commandParsers() {
    CONSTRUCT_ON_FIRST_USE(Parsers, []() {
      Parsers parsers;
      for (auto& f : Registry::FactoryRegistry<Factory>::factories()) {
        auto parser = f.second->createCommandParser();
        RELEASE_ASSERT(parser != nullptr, "Null built-in command parser");
        parsers.push_back(std::move(parser));
      }
      return parsers;
    }());
  }
};

} // namespace Formatter
} // namespace Envoy
