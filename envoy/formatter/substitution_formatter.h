#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"
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

class JsonStringSerializer;

/**
 * A sink for a single value to be serialized to JSON. The value is appended to the output of the
 * JsonStringSerializer.
 *
 * NOTE: Only one of the add*() methods could be called for only one time for a given ValueSink.
 * By this way the FormatterProvider won't be able to break the JSON format by adding multiple
 * values to the sink.
 */
class ValueSink {
public:
  ValueSink(JsonStringSerializer& serializer);

  /**
   * Add a value to the sink. The value will be serialized to JSON and appended to the output.
   * @param value supplies the value to add.
   */
  void addNumber(uint64_t value);
  void addNumber(double value);
  void addNumber(int64_t value);
  void addString(absl::string_view value);
  void addBool(bool value);

  /**
   * Add a null value to the sink explicitly. If this is called, the null value will always be
   * serialized to JSON and appended to the output.
   *
   * NOTE: If provider cannot extract a valid value, you can left the sink unmodified and the
   * caller will decide whether to add a default value or not. This should only be used when
   * the provider wants an explicit null value to be serialized.
   */
  void addNull();

  /**
   * Add a proto value or struct to the sink. The value will be serialized to JSON and appended to
   * the output. If this is called with Protobuf::Value::kNullValue, it will always be serialized
   * to JSON.
   *
   * @param value supplies the proto value to add.
   *
   * NOTE: If provider cannot extract a valid value, you can left the sink unmodified and the
   * caller will decide whether to add a default value or not.
   */
  void addValue(const Protobuf::Value& value);
  void addValue(const Protobuf::Struct& value);

  /**
   * @return bool true if a value was added to the sink explicitly.
   */
  bool consumed() const { return !serializer_.has_value(); }

private:
  /**
   * Takes the serializer out of the sink and marks the sink as consumed.
   * @return the serializer to write the value to, or nullptr if a value was already added to
   *         this sink.
   */
  JsonStringSerializer* consume();

  OptRef<JsonStringSerializer> serializer_;
};

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

  /**
   * Format the value with the given context and stream info and append it to the given sink.
   * @param sink supplies the sink to append the formatted value to.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   */
  virtual void formatValueTo(ValueSink& sink, const Context& context,
                             const StreamInfo::StreamInfo& stream_info) const {
    Protobuf::Value value = formatValue(context, stream_info);
    // In previous formatValue implementation, it will always return a Protobuf::Value even
    // the provider cannot extract a valid value.
    // For backward compatibility, we will ignore the null value and leave the sink unmodified.
    // The caller will decide whether to add a default value or not.
    if (value.kind_case() == Protobuf::Value::kNullValue ||
        value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
      return;
    }
    sink.addValue(value);
  }
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
