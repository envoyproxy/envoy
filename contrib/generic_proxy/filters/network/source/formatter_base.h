#pragma once

#include <string>

#include "envoy/stream_info/stream_info.h"

#include "source/common/config/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Interface for substitution formatter.
 * Formatters provide a complete substitution output line for the given context and stream info.
 */
template <class FormatterContext> class Formatter {
public:
  virtual ~Formatter() = default;

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

template <class FormatterContext> using FormatterPtr = std::unique_ptr<Formatter<FormatterContext>>;
template <class FormatterContext>
using FormatterConstSharedPtr = std::shared_ptr<const Formatter<FormatterContext>>;

/**
 * Interface for substitution provider.
 * FormatterProviders extract information from the given context and stream info.
 */
template <class FormatterContext> class FormatterProvider {
public:
  virtual ~FormatterProvider() = default;

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
using FormatterProviderPtr = std::unique_ptr<FormatterProvider<FormatterContext>>;

template <class FormatterContext> class CommandParser {
public:
  virtual ~CommandParser() = default;

  /**
   * Return a FormatterProviderPtr if command arg and max_length are correct for the formatter
   * provider associated with command.
   * @param command command name.
   * @param command_arg command specific argument. Empty if no argument is provided.
   * @param max_length length to which the output produced by FormatterProvider
   *                   should be truncated to (optional).
   *
   * @return FormattterProviderPtr substitution provider for the parsed command.
   */
  virtual FormatterProviderPtr<FormatterContext>
  parse(absl::string_view command, absl::string_view command_arg,
        absl::optional<size_t> max_length) const PURE;
};

template <class FormatterContext>
using CommandParserSharedPtr = std::shared_ptr<CommandParser<FormatterContext>>;

template <class FormatterContext>
using CommandParsers = std::vector<CommandParserSharedPtr<FormatterContext>>;

template <class FormatterContext> class CommandParserFactory : public Config::TypedFactory {
public:
  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserSharedPtr<FormatterContext>
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override {
    return fmt::format("envoy.{}.formatters", FormatterContext::category());
  }
};

template <class FormatterContext> class BuiltInCommandParsers {
public:
  static CommandParserSharedPtr<FormatterContext>
  parseCommandParser(const std::string& command, const std::string& subcommand,
                     absl::optional<size_t>& max_length);

  static void addCommandParser(CommandParserSharedPtr<FormatterContext> parser) {
    mutableCommandParsers().push_back(parser);
  }

  static const CommandParsers<FormatterContext>& commandParsers() {
    return mutableCommandParsers();
  }

private:
  static CommandParsers<FormatterContext>& mutableCommandParsers() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(CommandParsers<FormatterContext>);
  }
};

template <class FormatterContext> struct BuiltInCommandPaserRegister {
  BuiltInCommandPaserRegister(CommandParserSharedPtr<FormatterContext> parser) {
    BuiltInCommandParsers<FormatterContext>::addCommandParser(parser);
  }
};

#define REGISTER_BUILT_IN_COMMAND_PARSER(context, parser)                                          \
  static BuiltInCommandPaserRegister<context> register_##context##_##parser(parser);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
