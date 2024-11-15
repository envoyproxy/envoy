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

template <class FormatterContext>
using FormatterProviderBasePtr = std::unique_ptr<FormatterProviderBase<FormatterContext>>;

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
  parse(absl::string_view command, absl::string_view command_arg,
        absl::optional<size_t> max_length) const PURE;
};

template <class FormatterContext>
using CommandParserBasePtr = std::unique_ptr<CommandParserBase<FormatterContext>>;

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

  std::string category() const override {
    static constexpr absl::string_view HttpContextCategory = "http";
    if constexpr (FormatterContext::category() == HttpContextCategory) {
      return "envoy.formatter"; // Backward compatibility for HTTP.
    } else {
      return fmt::format("envoy.formatters.{}", FormatterContext::category());
    }
  }
};

template <class FormatterContext>
class BuiltInCommandParserFactoryBase : public Config::UntypedFactory {
public:
  std::string category() const override {
    return fmt::format("envoy.built_in_formatters.{}", FormatterContext::category());
  }

  /**
   * Creates a particular CommandParser implementation.
   */
  virtual CommandParserBasePtr<FormatterContext> createCommandParser() const PURE;
};

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
      for (const auto& factory : Envoy::Registry::FactoryRegistry<Factory>::factories()) {
        if (auto parser = factory.second->createCommandParser(); parser == nullptr) {
          ENVOY_BUG(false, fmt::format("Null built-in command parser: {}", factory.first));
          continue;
        } else {
          parsers.push_back(std::move(parser));
        }
      }
      return parsers;
    }());
  }
};

/**
 * Type placeholder for formatter providers that only require StreamInfo.
 */
struct StreamInfoOnlyFormatterContext {
  static constexpr absl::string_view category() { return "stream_info"; }
};

/**
 * Template specialization for formatter providers that only require StreamInfo.
 * The only difference from the main template is the providers inheriting from this class
 * will only take StreamInfo as input parameter.
 */
template <> class FormatterProviderBase<StreamInfoOnlyFormatterContext> {
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

using StreamInfoFormatterProvider = FormatterProviderBase<StreamInfoOnlyFormatterContext>;
using StreamInfoFormatterProviderPtr = FormatterProviderBasePtr<StreamInfoOnlyFormatterContext>;
using StreamInfoCommandParser = CommandParserBase<StreamInfoOnlyFormatterContext>;
using StreamInfoCommandParserPtr = CommandParserBasePtr<StreamInfoOnlyFormatterContext>;
using BuiltInStreamInfoCommandParserFactory =
    BuiltInCommandParserFactoryBase<StreamInfoOnlyFormatterContext>;
using BuiltInStreamInfoCommandParserFactoryHelper =
    BuiltInCommandParserFactoryHelper<StreamInfoOnlyFormatterContext>;

} // namespace Formatter
} // namespace Envoy
