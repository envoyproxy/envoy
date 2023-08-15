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
 * Interface for substitution formatter.
 * Formatters provide a complete substitution output line for the given context and stream info.
 */
template <class FormatterContext> class FormatterBase {
public:
  virtual ~FormatterBase() = default;

  /**
   * Extract a value from the provided context and stream info.
   * @param context supplies the context, which may be a request, response, or some other context.
   * @param info supplies the protocol independent stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual std::string format(const FormatterContext& context,
                             const StreamInfo::StreamInfo& info) const PURE;
};

template <class FormatterContext>
using FormatterBasePtr = std::unique_ptr<FormatterBase<FormatterContext>>;
template <class FormatterContext>
using FormatterBaseConstSharedPtr = std::shared_ptr<const FormatterBase<FormatterContext>>;

/**
 * Interface for substitution provider.
 * FormatterProviders extract information from the given context and stream info.
 */
template <class FormatterContext> class FormatterProviderBase {
public:
  virtual ~FormatterProviderBase() = default;

  /**
   * Extract a value from the provided context and stream info.
   * @param context supplies the context, which may be a request, response, or some other context.
   * @param info supplies the protocol independent stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual absl::optional<std::string> format(const FormatterContext& context,
                                             const StreamInfo::StreamInfo& info) const PURE;

  /**
   * Extract a value from the provided context and stream info.
   * @param context supplies the context, which may be a request, response, or some other context.
   * @param info supplies the protocol independent stream info.
   * @return ProtobufWkt::Value value containing a single value extracted from the given context and
   *         stream info.
   */
  virtual ProtobufWkt::Value formatValue(const FormatterContext& context,
                                         const StreamInfo::StreamInfo& info) const PURE;
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
                               Server::Configuration::CommonFactoryContext& context) PURE;

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
