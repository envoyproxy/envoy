#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Formatter {

/**
 * Interface for substitution formatter.
 * Formatters provide a complete substitution output line for the given headers/trailers/stream.
 */
class Formatter {
public:
  virtual ~Formatter() = default;

  /**
   * Return a formatted substitution line.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string format(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& response_headers,
                             const Http::ResponseTrailerMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             absl::string_view local_reply_body) const PURE;
};

using FormatterPtr = std::unique_ptr<Formatter>;
using FormatterConstSharedPtr = std::shared_ptr<const Formatter>;

/**
 * Interface for substitution provider.
 * FormatterProviders extract information from the given headers/trailers/stream.
 */
class FormatterProvider {
public:
  virtual ~FormatterProvider() = default;

  /**
   * Extract a value from the provided headers/trailers/stream.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   * the given headers/trailers/stream.
   */
  virtual absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                             const Http::ResponseHeaderMap& response_headers,
                                             const Http::ResponseTrailerMap& response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             absl::string_view local_reply_body) const PURE;
  /**
   * Extract a value from the provided headers/trailers/stream, preserving the value's type.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         headers/trailers/stream.
   */
  virtual ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const Http::ResponseTrailerMap& response_trailers,
                                         const StreamInfo::StreamInfo& stream_info,
                                         absl::string_view local_reply_body) const PURE;
};

using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

/**
 * Interface for command parser.
 * CommandParser returns a FormatterProviderPtr after successfully parsing an access log format
 * token, nullptr otherwise.
 */
class CommandParser {
public:
  virtual ~CommandParser() = default;

  /**
   * Return a FormatterProviderPtr if subcommand and max_length
   * are correct for the formatter provider associated
   * with command.
   * @param command - name of the FormatterProvider
   * @param subcommand - command specific data. (optional)
   * @param max_length - length to which the output produced by FormatterProvider
   *   should be truncated to (optional)
   *
   * @return FormattterProviderPtr substitution provider for the parsed command
   */
  virtual FormatterProviderPtr parse(const std::string& command, const std::string& subcommand,
                                     absl::optional<size_t>& max_length) const PURE;
};

using CommandParserPtr = std::unique_ptr<CommandParser>;

/**
 * Implemented by each custom CommandParser and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class CommandParserFactory : public Config::TypedFactory {
public:
  ~CommandParserFactory() override = default;

  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserPtr createCommandParserFromProto(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.formatter"; }
};

} // namespace Formatter
} // namespace Envoy
