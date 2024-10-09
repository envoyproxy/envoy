#include "source/common/formatter/http_specific_formatter.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
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

HeaderFormatter::HeaderFormatter(absl::string_view main_header,
                                 absl::string_view alternative_header,
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

  absl::string_view val = header->value().getStringView();
  val = SubstitutionFormatUtils::truncateStringView(val, max_length_);
  return std::string(val);
}

ProtobufWkt::Value HeaderFormatter::formatValue(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  absl::string_view val = header->value().getStringView();
  val = SubstitutionFormatUtils::truncateStringView(val, max_length_);
  return ValueUtil::stringValue(std::string(val));
}

ResponseHeaderFormatter::ResponseHeaderFormatter(absl::string_view main_header,
                                                 absl::string_view alternative_header,
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

RequestHeaderFormatter::RequestHeaderFormatter(absl::string_view main_header,
                                               absl::string_view alternative_header,
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

ResponseTrailerFormatter::ResponseTrailerFormatter(absl::string_view main_header,
                                                   absl::string_view alternative_header,
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

ProtobufWkt::Value TraceIDFormatter::formatValueWithContext(const HttpFormatterContext& context,
                                                            const StreamInfo::StreamInfo&) const {
  auto trace_id = context.activeSpan().getTraceId();
  if (trace_id.empty()) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }
  return ValueUtil::stringValue(trace_id);
}

absl::optional<std::string>
TraceIDFormatter::formatWithContext(const HttpFormatterContext& context,
                                    const StreamInfo::StreamInfo&) const {
  auto trace_id = context.activeSpan().getTraceId();
  if (trace_id.empty()) {
    return absl::nullopt;
  }
  return trace_id;
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

  throw EnvoyException("GrpcStatusFormatter only supports CAMEL_STRING, SNAKE_STRING or NUMBER.");
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

const BuiltInHttpCommandParser::FormatterProviderLookupTbl&
BuiltInHttpCommandParser::getKnownFormatters() {
  CONSTRUCT_ON_FIRST_USE(
      FormatterProviderLookupTbl,
      {{"REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](absl::string_view format, absl::optional<size_t> max_length) {
           auto result = SubstitutionFormatUtils::parseSubcommandHeaders(format);
           THROW_IF_NOT_OK_REF(result.status());
           return std::make_unique<RequestHeaderFormatter>(result.value().first,
                                                           result.value().second, max_length);
         }}},
       {"RESP",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](absl::string_view format, absl::optional<size_t> max_length) {
           auto result = SubstitutionFormatUtils::parseSubcommandHeaders(format);
           THROW_IF_NOT_OK_REF(result.status());
           return std::make_unique<ResponseHeaderFormatter>(result.value().first,
                                                            result.value().second, max_length);
         }}},
       {"TRAILER",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](absl::string_view format, absl::optional<size_t> max_length) {
           auto result = SubstitutionFormatUtils::parseSubcommandHeaders(format);
           THROW_IF_NOT_OK_REF(result.status());
           return std::make_unique<ResponseTrailerFormatter>(result.value().first,
                                                             result.value().second, max_length);
         }}},
       {"LOCAL_REPLY_BODY",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<LocalReplyBodyFormatter>();
         }}},
       {"ACCESS_LOG_TYPE",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<AccessLogTypeFormatter>();
         }}},
       {"GRPC_STATUS",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](absl::string_view format, absl::optional<size_t>) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::parseFormat(format));
         }}},
       {"GRPC_STATUS_NUMBER",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>(),
                                                        GrpcStatusFormatter::Number);
         }}},
       {"REQUEST_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::RequestHeaders);
         }}},
       {"RESPONSE_HEADERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseHeaders);
         }}},
       {"RESPONSE_TRAILERS_BYTES",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<HeadersByteSizeFormatter>(
               HeadersByteSizeFormatter::HeaderType::ResponseTrailers);
         }}},
       {"STREAM_INFO_REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](absl::string_view format, absl::optional<size_t> max_length) {
           auto result = SubstitutionFormatUtils::parseSubcommandHeaders(format);
           THROW_IF_NOT_OK_REF(result.status());
           return std::make_unique<RequestHeaderFormatter>(result.value().first,
                                                           result.value().second, max_length);
         }}},
       {"TRACE_ID",
        {CommandSyntaxChecker::COMMAND_ONLY, [](absl::string_view, absl::optional<size_t>) {
           return std::make_unique<TraceIDFormatter>();
         }}}});
}

FormatterProviderPtr BuiltInHttpCommandParser::parse(absl::string_view command,
                                                     absl::string_view subcommand,
                                                     absl::optional<size_t> max_length) const {
  const FormatterProviderLookupTbl& providers = getKnownFormatters();

  auto it = providers.find(command);

  if (it == providers.end()) {
    return nullptr;
  }

  // Check flags for the command.
  THROW_IF_NOT_OK(
      CommandSyntaxChecker::verifySyntax((*it).second.first, command, subcommand, max_length));

  // Create a pointer to the formatter by calling a function
  // associated with formatter's name.
  return (*it).second.second(subcommand, max_length);
}

std::string DefaultBuiltInHttpCommandParserFactory::name() const {
  return "envoy.built_in_formatters.http.default";
}

CommandParserPtr DefaultBuiltInHttpCommandParserFactory::createCommandParser() const {
  return std::make_unique<BuiltInHttpCommandParser>();
}

REGISTER_FACTORY(DefaultBuiltInHttpCommandParserFactory, BuiltInHttpCommandParserFactory);

} // namespace Formatter
} // namespace Envoy
