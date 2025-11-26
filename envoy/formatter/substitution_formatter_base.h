#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Formatter {

/**
 * Interface for multiple protocols/modules formatters.
 */
class Formatter {
public:
  virtual ~Formatter() = default;

  /**
   * Return a formatted substitution line.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string format(const Context& context,
                             const StreamInfo::StreamInfo& stream_info) const PURE;
};

using FormatterPtr = std::unique_ptr<Formatter>;
using FormatterConstSharedPtr = std::shared_ptr<const Formatter>;

/**
 * Interface for multiple protocols/modules formatter providers.
 */
class FormatterProvider {
public:
  virtual ~FormatterProvider() = default;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual absl::optional<std::string> format(const Context& context,
                                             const StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return Protobuf::Value containing a single value extracted from the given
   *         context and stream info.
   */
  virtual Protobuf::Value formatValue(const Context& context,
                                      const StreamInfo::StreamInfo& stream_info) const PURE;
};

using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

class CommandParser {
public:
  virtual ~CommandParser() = default;

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
  virtual FormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                                     absl::optional<size_t> max_length) const PURE;
};

using CommandParserPtr = std::unique_ptr<CommandParser>;
using CommandParserPtrVector = std::vector<CommandParserPtr>;

class CommandParserFactory : public Config::TypedFactory {
public:
  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.formatter"; }
};

class BuiltInCommandParserFactory : public Config::UntypedFactory {
public:
  std::string category() const override { return "envoy.built_in_formatters"; }

  /**
   * Creates a particular CommandParser implementation.
   */
  virtual CommandParserPtr createCommandParser() const PURE;
};

/**
 * Helper class to get all built-in command parsers for a given formatter context.
 */
class BuiltInCommandParserFactoryHelper {
public:
  using Factory = BuiltInCommandParserFactory;
  using Parsers = std::vector<CommandParserPtr>;

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

} // namespace Formatter
} // namespace Envoy
