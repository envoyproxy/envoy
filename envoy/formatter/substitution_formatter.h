#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

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

  /**
   * Append the formatted substitution line to the given sink. Callers that format repeatedly
   * can hand in a reused buffer and avoid the allocation that format() performs on every call.
   * Implementations that build the line incrementally should override this; the default simply
   * appends the result of format().
   * @param sink supplies the string the substitution line is appended to.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   */
  virtual void formatTo(std::string& sink, const Context& context,
                        const StreamInfo::StreamInfo& stream_info) const {
    sink.append(format(context, stream_info));
  }
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
   * @return std::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual std::optional<std::string> format(const Context& context,
                                            const StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Append the extracted value to the given sink. This is the allocation-free counterpart of
   * format() and should be overridden by providers that can write their value directly into the
   * sink. The default implementation appends the result of format().
   * @param sink supplies the string the value is appended to. It is left unmodified if no value
   *        is extracted.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return bool true if a value was extracted and appended to the sink.
   */
  virtual bool formatTo(std::string& sink, const Context& context,
                        const StreamInfo::StreamInfo& stream_info) const {
    const std::optional<std::string> value = format(context, stream_info);
    if (!value.has_value()) {
      return false;
    }
    sink.append(*value);
    return true;
  }

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
   * @return absl::StatusOr<FormatterProviderPtr> substitution provider for the parsed command or an
   * error status.
   */
  virtual absl::StatusOr<FormatterProviderPtr> parse(absl::string_view command,
                                                     absl::string_view command_arg,
                                                     std::optional<size_t> max_length) const PURE;
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

} // namespace Formatter
} // namespace Envoy
