#include "source/common/formatter/http_specific_formatter.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Formatter {

HttpFormatterContext::HttpFormatterContext(const Http::RequestHeaderMap* request_headers,
                                           const Http::ResponseHeaderMap* response_headers,
                                           const Http::ResponseTrailerMap* response_trailers,
                                           absl::string_view local_reply_body,
                                           AccessLog::AccessLogType log_type)
    : request_headers_(request_headers), response_headers_(response_headers),
      response_trailers_(response_trailers), local_reply_body_(local_reply_body),
      log_type_(log_type) {}

const Http::RequestHeaderMap& HttpFormatterContext::requestHeaders() const {
  return request_headers_ != nullptr ? *request_headers_
                                     : *Http::StaticEmptyHeaders::get().request_headers;
}
const Http::ResponseHeaderMap& HttpFormatterContext::responseHeaders() const {
  return response_headers_ != nullptr ? *response_headers_
                                      : *Http::StaticEmptyHeaders::get().response_headers;
}
const Http::ResponseTrailerMap& HttpFormatterContext::responseTrailers() const {
  return response_trailers_ != nullptr ? *response_trailers_
                                       : *Http::StaticEmptyHeaders::get().response_trailers;
}

absl::string_view HttpFormatterContext::localReplyBody() const { return local_reply_body_; }
AccessLog::AccessLogType HttpFormatterContext::accessLogType() const { return log_type_; }

absl::optional<std::string>
LocalReplyBodyFormatter::formatWithContext(const HttpFormatterContext& context,
                                           const StreamInfo::StreamInfo&) const {
  return std::string(context.localReplyBody());
}

ProtobufWkt::Value
LocalReplyBodyFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                                const StreamInfo::StreamInfo&) const {
  return ValueUtil::stringValue(std::string(context.localReplyBody()));
}

absl::optional<std::string>
AccessLogTypeFormatter::formatWithContext(const HttpFormatterContext& context,
                                          const StreamInfo::StreamInfo&) const {
  return AccessLogType_Name(context.accessLogType());
}

ProtobufWkt::Value
AccessLogTypeFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                               const StreamInfo::StreamInfo&) const {
  return ValueUtil::stringValue(AccessLogType_Name(context.accessLogType()));
}

HeaderFormatter::HeaderFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

const Http::HeaderEntry* HeaderFormatter::findHeader(const Http::HeaderMap& headers) const {
  const auto header = headers.get(main_header_);

  if (header.empty() && !alternative_header_.get().empty()) {
    const auto alternate_header = headers.get(alternative_header_);
    // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially log all header values.
    return alternate_header.empty() ? nullptr : alternate_header[0];
  }

  return header.empty() ? nullptr : header[0];
}

absl::optional<std::string> HeaderFormatter::format(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return absl::nullopt;
  }

  std::string val = std::string(header->value().getStringView());
  SubstitutionFormatUtils::truncate(val, max_length_);
  return val;
}

ProtobufWkt::Value HeaderFormatter::formatValue(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  std::string val = std::string(header->value().getStringView());
  SubstitutionFormatUtils::truncate(val, max_length_);
  return ValueUtil::stringValue(val);
}

ResponseHeaderFormatter::ResponseHeaderFormatter(const std::string& main_header,
                                                 const std::string& alternative_header,
                                                 absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
ResponseHeaderFormatter::formatWithContext(const HttpFormatterContext& context,
                                           const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(context.responseHeaders());
}

ProtobufWkt::Value
ResponseHeaderFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                                const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(context.responseHeaders());
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
RequestHeaderFormatter::formatWithContext(const HttpFormatterContext& context,
                                          const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(context.requestHeaders());
}

ProtobufWkt::Value
RequestHeaderFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                               const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(context.requestHeaders());
}

ResponseTrailerFormatter::ResponseTrailerFormatter(const std::string& main_header,
                                                   const std::string& alternative_header,
                                                   absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
ResponseTrailerFormatter::formatWithContext(const HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(context.responseTrailers());
}

ProtobufWkt::Value
ResponseTrailerFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                                 const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(context.responseTrailers());
}

HeadersByteSizeFormatter::HeadersByteSizeFormatter(const HeaderType header_type)
    : header_type_(header_type) {}

uint64_t HeadersByteSizeFormatter::extractHeadersByteSize(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers) const {
  switch (header_type_) {
  case HeaderType::RequestHeaders:
    return request_headers.byteSize();
  case HeaderType::ResponseHeaders:
    return response_headers.byteSize();
  case HeaderType::ResponseTrailers:
    return response_trailers.byteSize();
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::optional<std::string>
HeadersByteSizeFormatter::formatWithContext(const HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo&) const {
  return absl::StrCat(extractHeadersByteSize(context.requestHeaders(), context.responseHeaders(),
                                             context.responseTrailers()));
}

ProtobufWkt::Value
HeadersByteSizeFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                                 const StreamInfo::StreamInfo&) const {
  return ValueUtil::numberValue(extractHeadersByteSize(
      context.requestHeaders(), context.responseHeaders(), context.responseTrailers()));
}

GrpcStatusFormatter::Format GrpcStatusFormatter::parseFormat(absl::string_view format) {
  if (format.empty() || format == "CAMEL_STRING") {
    return GrpcStatusFormatter::CamelString;
  }

  if (format == "SNAKE_STRING") {
    return GrpcStatusFormatter::SnakeString;
  }
  if (format == "NUMBER") {
    return GrpcStatusFormatter::Number;
  }

  throwEnvoyExceptionOrPanic(
      "GrpcStatusFormatter only supports CAMEL_STRING, SNAKE_STRING or NUMBER.");
}

GrpcStatusFormatter::GrpcStatusFormatter(const std::string& main_header,
                                         const std::string& alternative_header,
                                         absl::optional<size_t> max_length, Format format)
    : HeaderFormatter(main_header, alternative_header, max_length), format_(format) {}

absl::optional<std::string>
GrpcStatusFormatter::formatWithContext(const HttpFormatterContext& context,
                                       const StreamInfo::StreamInfo& info) const {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_grpc_header_before_log_grpc_status")) {
    if (!Grpc::Common::isGrpcRequestHeaders(context.requestHeaders())) {
      return absl::nullopt;
    }
  }
  const auto grpc_status = Grpc::Common::getGrpcStatus(context.responseTrailers(),
                                                       context.responseHeaders(), info, true);
  if (!grpc_status.has_value()) {
    return absl::nullopt;
  }
  switch (format_) {
  case CamelString: {
    const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
    if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
      return std::to_string(grpc_status.value());
    }
    return grpc_status_message;
  }
  case SnakeString: {
    const auto grpc_status_message =
        absl::StatusCodeToString(static_cast<absl::StatusCode>(grpc_status.value()));
    if (grpc_status_message == EMPTY_STRING) {
      return std::to_string(grpc_status.value());
    }
    return grpc_status_message;
  }
  case Number: {
    return std::to_string(grpc_status.value());
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

ProtobufWkt::Value
GrpcStatusFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo& info) const {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_grpc_header_before_log_grpc_status")) {
    if (!Grpc::Common::isGrpcRequestHeaders(context.requestHeaders())) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
  }
  const auto grpc_status = Grpc::Common::getGrpcStatus(context.responseTrailers(),
                                                       context.responseHeaders(), info, true);
  if (!grpc_status.has_value()) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  switch (format_) {
  case CamelString: {
    const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
    if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
      return ValueUtil::stringValue(std::to_string(grpc_status.value()));
    }
    return ValueUtil::stringValue(grpc_status_message);
  }
  case SnakeString: {
    const auto grpc_status_message =
        absl::StatusCodeToString(static_cast<absl::StatusCode>(grpc_status.value()));
    if (grpc_status_message == EMPTY_STRING) {
      return ValueUtil::stringValue(std::to_string(grpc_status.value()));
    }
    return ValueUtil::stringValue(grpc_status_message);
  }
  case Number: {
    return ValueUtil::numberValue(grpc_status.value());
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

StreamInfoRequestHeaderFormatter::StreamInfoRequestHeaderFormatter(
    const std::string& main_header, const std::string& alternative_header,
    absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string> StreamInfoRequestHeaderFormatter::formatWithContext(
    const HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) const {
  return HeaderFormatter::format(*stream_info.getRequestHeaders());
}

ProtobufWkt::Value StreamInfoRequestHeaderFormatter::formatValueWithContext(
    const HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) const {
  return HeaderFormatter::formatValue(*stream_info.getRequestHeaders());
}

const HttpBuiltInCommandParser::FormatterProviderLookupTbl&
HttpBuiltInCommandParser::getKnownFormatters() {
  CONSTRUCT_ON_FIRST_USE(
      FormatterProviderLookupTbl,
      {{"REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           SubstitutionFormatUtils::parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header,
                                                           max_length);
         }}},
       {"RESP",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           SubstitutionFormatUtils::parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseHeaderFormatter>(main_header, alternative_header,
                                                            max_length);
         }}},
       {"TRAILER",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           SubstitutionFormatUtils::parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseTrailerFormatter>(main_header, alternative_header,
                                                             max_length);
         }}},
       {"LOCAL_REPLY_BODY",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<LocalReplyBodyFormatter>();
         }}},
       {"ACCESS_LOG_TYPE",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<AccessLogTypeFormatter>();
         }}},
       {"GRPC_STATUS",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::parseFormat(format));
         }}},
       {"GRPC_STATUS_NUMBER",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, const absl::optional<size_t>&) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::Number);
         }}},
       {"REQUEST_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::RequestHeaders);
         }}},
       {"RESPONSE_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseHeaders);
         }}},
       {"RESPONSE_TRAILERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseTrailers);
         }}},
       {"STREAM_INFO_REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;
           SubstitutionFormatUtils::parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header,
                                                           max_length);
         }}}});
}

FormatterProviderPtr HttpBuiltInCommandParser::parse(const std::string& command,
                                                     const std::string& subcommand,
                                                     absl::optional<size_t>& max_length) const {
  const FormatterProviderLookupTbl& providers = getKnownFormatters();

  auto it = providers.find(command);

  if (it == providers.end()) {
    return nullptr;
  }

  // Check flags for the command.
  CommandSyntaxChecker::verifySyntax((*it).second.first, command, subcommand, max_length);

  // Create a pointer to the formatter by calling a function
  // associated with formatter's name.
  return (*it).second.second(subcommand, max_length);
}

REGISTER_BUILT_IN_COMMAND_PARSER(HttpFormatterContext, HttpBuiltInCommandParser);

static const std::string DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

FormatterPtr HttpSubstitutionFormatUtils::defaultSubstitutionFormatter() {
  return std::make_unique<Envoy::Formatter::FormatterImpl>(DEFAULT_FORMAT, false);
}

} // namespace Formatter
} // namespace Envoy
