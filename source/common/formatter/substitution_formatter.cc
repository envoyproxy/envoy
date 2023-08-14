#include "source/common/formatter/substitution_formatter.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common//formatter/http_specific_formatter.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/http_specific_formatter.h"
#include "source/common/formatter/stream_info_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

const std::string SubstitutionFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

void CommandSyntaxChecker::verifySyntax(CommandSyntaxFlags flags, const std::string& command,
                                        const std::string& subcommand,
                                        const absl::optional<size_t>& length) {
  if ((flags == COMMAND_ONLY) && ((subcommand.length() != 0) || length.has_value())) {
    throw EnvoyException(fmt::format("{} does not take any parameters or length", command));
  }

  if ((flags & PARAMS_REQUIRED).any() && (subcommand.length() == 0)) {
    throw EnvoyException(fmt::format("{} requires parameters", command));
  }

  if ((flags & LENGTH_ALLOWED).none() && length.has_value()) {
    throw EnvoyException(fmt::format("{} does not allow length to be specified.", command));
  }
}

FormatterPtr SubstitutionFormatUtils::defaultSubstitutionFormatter() {
  return std::make_unique<Envoy::Formatter::FormatterImpl>(DEFAULT_FORMAT, false);
}

const absl::optional<std::reference_wrapper<const std::string>>
SubstitutionFormatUtils::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return absl::nullopt;
}

const std::string&
SubstitutionFormatUtils::protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return DefaultUnspecifiedValueString;
}

const absl::optional<std::string> SubstitutionFormatUtils::getHostname() {
#ifdef HOST_NAME_MAX
  const size_t len = HOST_NAME_MAX;
#else
  // This is notably the case in OSX.
  const size_t len = 255;
#endif
  char name[len];
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result = os_sys_calls.gethostname(name, len);

  absl::optional<std::string> hostname;
  if (result.return_value_ == 0) {
    hostname = name;
  }

  return hostname;
}

const std::string SubstitutionFormatUtils::getHostnameOrDefault() {
  absl::optional<std::string> hostname = getHostname();
  if (hostname.has_value()) {
    return hostname.value();
  }
  return DefaultUnspecifiedValueString;
}

const ProtobufWkt::Value& SubstitutionFormatUtils::unspecifiedValue() {
  return ValueUtil::nullValue();
}

void SubstitutionFormatUtils::truncate(std::string& str, absl::optional<size_t> max_length) {
  if (!max_length) {
    return;
  }

  if (str.length() > max_length.value()) {
    str.resize(max_length.value());
  }
}

FormatterImpl::FormatterImpl(const std::string& format, bool omit_empty_values)
    : empty_value_string_(omit_empty_values ? EMPTY_STRING : DefaultUnspecifiedValueString) {
  providers_ = SubstitutionFormatParser::parse(format);
}

FormatterImpl::FormatterImpl(const std::string& format, bool omit_empty_values,
                             const std::vector<CommandParserPtr>& command_parsers)
    : empty_value_string_(omit_empty_values ? EMPTY_STRING : DefaultUnspecifiedValueString) {
  providers_ = SubstitutionFormatParser::parse(format, command_parsers);
}

std::string FormatterImpl::format(const Http::RequestHeaderMap& request_headers,
                                  const Http::ResponseHeaderMap& response_headers,
                                  const Http::ResponseTrailerMap& response_trailers,
                                  const StreamInfo::StreamInfo& stream_info,
                                  absl::string_view local_reply_body,
                                  AccessLog::AccessLogType access_log_type) const {
  std::string log_line;
  log_line.reserve(256);

  for (const FormatterProviderPtr& provider : providers_) {
    const auto bit = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body, access_log_type);
    log_line += bit.value_or(empty_value_string_);
  }

  return log_line;
}

std::string JsonFormatterImpl::format(const Http::RequestHeaderMap& request_headers,
                                      const Http::ResponseHeaderMap& response_headers,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      absl::string_view local_reply_body,
                                      AccessLog::AccessLogType access_log_type) const {
  const ProtobufWkt::Struct output_struct =
      struct_formatter_.format(request_headers, response_headers, response_trailers, stream_info,
                               local_reply_body, access_log_type);

#ifdef ENVOY_ENABLE_YAML
  const std::string log_line =
      MessageUtil::getJsonStringFromMessageOrError(output_struct, false, true);
#else
  IS_ENVOY_BUG("Json support compiled out");
  const std::string log_line = "";
#endif
  return absl::StrCat(log_line, "\n");
}

StructFormatter::StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                                 bool omit_empty_values,
                                 const std::vector<CommandParserPtr>& commands)
    : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
      empty_value_(omit_empty_values_ ? EMPTY_STRING : DefaultUnspecifiedValueString),
      struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

StructFormatter::StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                                 bool omit_empty_values)
    : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
      empty_value_(omit_empty_values_ ? EMPTY_STRING : DefaultUnspecifiedValueString),
      struct_output_format_(FormatBuilder().toFormatMapValue(format_mapping)) {}

StructFormatter::StructFormatMapWrapper
StructFormatter::FormatBuilder::toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
  auto output = std::make_unique<StructFormatMap>();
  for (const auto& pair : struct_format.fields()) {
    switch (pair.second.kind_case()) {
    case ProtobufWkt::Value::kStringValue:
      output->emplace(pair.first, toFormatStringValue(pair.second.string_value()));
      break;

    case ProtobufWkt::Value::kStructValue:
      output->emplace(pair.first, toFormatMapValue(pair.second.struct_value()));
      break;

    case ProtobufWkt::Value::kListValue:
      output->emplace(pair.first, toFormatListValue(pair.second.list_value()));
      break;

    case ProtobufWkt::Value::kNumberValue:
      output->emplace(pair.first, toFormatNumberValue(pair.second.number_value()));
      break;

    default:
      throw EnvoyException("Only string values, nested structs, list values and number values are "
                           "supported in structured access log format.");
    }
  }
  return {std::move(output)};
}

StructFormatter::StructFormatListWrapper StructFormatter::FormatBuilder::toFormatListValue(
    const ProtobufWkt::ListValue& list_value_format) const {
  auto output = std::make_unique<StructFormatList>();
  for (const auto& value : list_value_format.values()) {
    switch (value.kind_case()) {
    case ProtobufWkt::Value::kStringValue:
      output->emplace_back(toFormatStringValue(value.string_value()));
      break;

    case ProtobufWkt::Value::kStructValue:
      output->emplace_back(toFormatMapValue(value.struct_value()));
      break;

    case ProtobufWkt::Value::kListValue:
      output->emplace_back(toFormatListValue(value.list_value()));
      break;

    case ProtobufWkt::Value::kNumberValue:
      output->emplace_back(toFormatNumberValue(value.number_value()));
      break;

    default:
      throw EnvoyException("Only string values, nested structs, list values and number values are "
                           "supported in structured access log format.");
    }
  }
  return {std::move(output)};
}

std::vector<FormatterProviderPtr>
StructFormatter::FormatBuilder::toFormatStringValue(const std::string& string_format) const {
  std::vector<CommandParserPtr> commands;
  return SubstitutionFormatParser::parse(string_format, commands_.value_or(commands));
}

std::vector<FormatterProviderPtr>
StructFormatter::FormatBuilder::toFormatNumberValue(double value) const {
  std::vector<FormatterProviderPtr> formatters;
  formatters.emplace_back(FormatterProviderPtr{new PlainNumberFormatter(value)});
  return formatters;
}

ProtobufWkt::Value StructFormatter::providersCallback(
    const std::vector<FormatterProviderPtr>& providers,
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo& stream_info,
    absl::string_view local_reply_body, AccessLog::AccessLogType access_log_type) const {
  ASSERT(!providers.empty());
  if (providers.size() == 1) {
    const auto& provider = providers.front();
    if (preserve_types_) {
      return provider->formatValue(request_headers, response_headers, response_trailers,
                                   stream_info, local_reply_body, access_log_type);
    }

    if (omit_empty_values_) {
      return ValueUtil::optionalStringValue(provider->format(request_headers, response_headers,
                                                             response_trailers, stream_info,
                                                             local_reply_body, access_log_type));
    }

    const auto str = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body, access_log_type);
    return ValueUtil::stringValue(str.value_or(DefaultUnspecifiedValueString));
  }
  // Multiple providers forces string output.
  std::string str;
  for (const auto& provider : providers) {
    const auto bit = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body, access_log_type);
    str += bit.value_or(empty_value_);
  }
  return ValueUtil::stringValue(str);
}

ProtobufWkt::Value StructFormatter::structFormatMapCallback(
    const StructFormatter::StructFormatMapWrapper& format_map,
    const StructFormatter::StructFormatMapVisitor& visitor) const {
  ProtobufWkt::Struct output;
  auto* fields = output.mutable_fields();
  for (const auto& pair : *format_map.value_) {
    ProtobufWkt::Value value = absl::visit(visitor, pair.second);
    if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
      continue;
    }
    (*fields)[pair.first] = value;
  }
  if (omit_empty_values_ && output.fields().empty()) {
    return ValueUtil::nullValue();
  }
  return ValueUtil::structValue(output);
}

ProtobufWkt::Value StructFormatter::structFormatListCallback(
    const StructFormatter::StructFormatListWrapper& format_list,
    const StructFormatter::StructFormatMapVisitor& visitor) const {
  std::vector<ProtobufWkt::Value> output;
  for (const auto& val : *format_list.value_) {
    ProtobufWkt::Value value = absl::visit(visitor, val);
    if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
      continue;
    }
    output.push_back(value);
  }
  return ValueUtil::listValue(output);
}

ProtobufWkt::Struct StructFormatter::format(const Http::RequestHeaderMap& request_headers,
                                            const Http::ResponseHeaderMap& response_headers,
                                            const Http::ResponseTrailerMap& response_trailers,
                                            const StreamInfo::StreamInfo& stream_info,
                                            absl::string_view local_reply_body,
                                            AccessLog::AccessLogType access_log_type) const {
  StructFormatMapVisitor visitor{
      [&](const std::vector<FormatterProviderPtr>& providers) {
        return providersCallback(providers, request_headers, response_headers, response_trailers,
                                 stream_info, local_reply_body, access_log_type);
      },
      [&, this](const StructFormatter::StructFormatMapWrapper& format_map) {
        return structFormatMapCallback(format_map, visitor);
      },
      [&, this](const StructFormatter::StructFormatListWrapper& format_list) {
        return structFormatListCallback(format_list, visitor);
      },
  };
  return structFormatMapCallback(struct_output_format_, visitor).struct_value();
}

void SubstitutionFormatParser::parseSubcommandHeaders(const std::string& subcommand,
                                                      std::string& main_header,
                                                      std::string& alternative_header) {
  // subs is used only to check if there are more than 2 headers separated by '?'.
  std::vector<std::string> subs;
  alternative_header = "";
  parseSubcommand(subcommand, '?', main_header, alternative_header, subs);
  if (!subs.empty()) {
    throw EnvoyException(
        // Header format rules support only one alternative header.
        // docs/root/configuration/observability/access_log/access_log.rst#format-rules
        absl::StrCat("More than 1 alternative header specified in token: ", subcommand));
  }

  // The main and alternative header should not contain invalid characters {NUL, LR, CF}.
  if (!Envoy::Http::validHeaderString(main_header) ||
      !Envoy::Http::validHeaderString(alternative_header)) {
    throw EnvoyException("Invalid header configuration. Format string contains null or newline.");
  }
}

std::vector<FormatterProviderPtr> SubstitutionFormatParser::parse(const std::string& format) {
  return SubstitutionFormatParser::parse(format, {});
}

// TODO(derekargueta): #2967 - Rewrite SubstitutionFormatter with parser library & formal grammar
std::vector<FormatterProviderPtr>
SubstitutionFormatParser::parse(const std::string& format,
                                const std::vector<CommandParserPtr>& commands) {
  std::string current_token;
  std::vector<FormatterProviderPtr> formatters;

  // The following regex is used to check validity of the formatter command and to
  // extract groups.
  // The formatter command has the following format:
  //    % COMMAND(SUBCOMMAND):LENGTH%
  // % signs at the beginning and end are used by parser to find next COMMAND.
  // COMMAND must always be present and must consist of characters: "A-Z", "0-9" or "_".
  // SUBCOMMAND presence depends on the COMMAND. Format is flexible but cannot contain ")".:
  // - for some commands SUBCOMMAND is not allowed (for example %PROTOCOL%)
  // - for some commands SUBCOMMAND is required (for example %REQ(:AUTHORITY)%, just %REQ% will
  // cause error)
  // - for some commands SUBCOMMAND is optional (for example %START_TIME% and
  // %START_TIME(%f.%1f.%2f.%3f)% are both correct).
  // LENGTH presence depends on the command. Some
  // commands allow LENGTH to be specified, so not. Regex is used to validate the syntax and also to
  // extract values for COMMAND, SUBCOMMAND and LENGTH.
  //
  // Below is explanation of capturing and non-capturing groups. Non-capturing groups are used
  // to specify that certain part of the formatter command is optional and should contain specific
  // characters. Capturing groups are used to extract the values when regex is matched against
  // formatter command string.
  //
  // clang-format off
  // Non-capturing group specifying optional :LENGTH -------------------------------------
  //                                                                                      |
  // Non-capturing group specifying optional (SUBCOMMAND)------------------               |
  //                                                                       |              |
  // Non-capturing group specifying mandatory COMMAND                      |              |
  //  which uses only A-Z, 0-9 and _ characters     -----                  |              |
  //  Group is used only to specify allowed characters.  |                 |              |
  //                                                     |                 |              |
  //                                                     |                 |              |
  //                                             _________________  _______________  _____________
  //                                             |               |  |             |  |           |
  const std::regex command_w_args_regex(R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\(([^\)]*)\))?(?::([0-9]+))?%)EOF");
  //                                            |__________________|     |______|        |______|
  //                                                     |                   |              |
  // Capturing group specifying COMMAND -----------------                    |              |
  // The index of this group is 1.                                           |              |
  //                                                                         |              |
  // Capturing group for SUBCOMMAND. If present, it will --------------------               |
  // contain SUBCOMMAND without "(" and ")". The index                                      |
  // of SUBCOMMAND group is 2.                                                              |
  //                                                                                        |
  // Capturing group for LENGTH. If present, it will ----------------------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on

  for (size_t pos = 0; pos < format.size(); ++pos) {
    if (format[pos] != '%') {
      current_token += format[pos];
      continue;
    }

    // escape '%%'
    if (format.size() > pos + 1) {
      if (format[pos + 1] == '%') {
        current_token += '%';
        pos++;
        continue;
      }
    }

    if (!current_token.empty()) {
      formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
      current_token = "";
    }

    std::smatch m;
    const std::string search_space = format.substr(pos);
    if (!std::regex_search(search_space, m, command_w_args_regex)) {
      throw EnvoyException(fmt::format(
          "Incorrect configuration: {}. Couldn't find valid command at position {}", format, pos));
    }

    const std::string match = m.str(0);
    // command is at at index 1.
    const std::string command = m.str(1);
    // subcommand is at index 2.
    const std::string subcommand = m.str(2);
    // optional length is at index 3. If present, validate that it is valid integer.
    absl::optional<size_t> max_length;
    if (m.str(3).length() != 0) {
      size_t length_value;
      if (!absl::SimpleAtoi(m.str(3), &length_value)) {
        throw EnvoyException(absl::StrCat("Length must be an integer, given: ", m.str(3)));
      }
      max_length = length_value;
    }
    std::vector<std::string> path;

    const size_t command_end_position = pos + m.str(0).length() - 1;

    auto formatter =
        HttpBuiltInCommandParser::builtInCommandParser().parse(command, subcommand, max_length);
    if (formatter) {
      formatters.push_back(std::move(formatter));
    } else {
      // Check formatter extensions. These are used for anything not provided by the built-in
      // operators, e.g.: specialized formatting, computing stats from request/response headers
      // or from stream info, etc.
      bool added = false;
      for (const auto& cmd : commands) {
        auto formatter = cmd->parse(command, subcommand, max_length);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      if (!added) {
        formatters.emplace_back(
            FormatterProviderPtr{new StreamInfoFormatter(command, subcommand, max_length)});
      }
    }

    pos = command_end_position;
  }

  if (!current_token.empty() || format.empty()) {
    // Create a PlainStringFormatter with the final string literal. If the format string was empty,
    // this creates a PlainStringFormatter with an empty string.
    formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

} // namespace Formatter
} // namespace Envoy
