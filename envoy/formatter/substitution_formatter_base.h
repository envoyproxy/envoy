#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
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
  parse(const std::string& command, const std::string& command_arg,
        absl::optional<size_t>& max_length) const PURE;
};

template <class FormatterContext>
using CommandParserBasePtr = std::unique_ptr<CommandParserBase<FormatterContext>>;

template <class FormatterContext>
using CommandParsersBase = std::vector<CommandParserBasePtr<FormatterContext>>;

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
    return fmt::format("envoy.{}.formatters", FormatterContext::category());
  }
};

template <class FormatterContext> class BuiltInCommandParsersBase {
public:
  static void addCommandParser(CommandParserBasePtr<FormatterContext> parser) {
    mutableCommandParsers().push_back(std::move(parser));
  }

  static const CommandParsersBase<FormatterContext>& commandParsers() {
    return mutableCommandParsers();
  }

private:
  static CommandParsersBase<FormatterContext>& mutableCommandParsers() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(CommandParsersBase<FormatterContext>);
  }
};

template <class FormatterContext> struct BuiltInCommandPaserRegister {
  BuiltInCommandPaserRegister(CommandParserBasePtr<FormatterContext> parser) {
    BuiltInCommandParsersBase<FormatterContext>::addCommandParser(std::move(parser));
  }
};

#define REGISTER_BUILT_IN_COMMAND_PARSER(context, parser)                                          \
  static BuiltInCommandPaserRegister<context> register_##context##_##parser{                       \
      std::make_unique<parser>()};

} // namespace Formatter
} // namespace Envoy
