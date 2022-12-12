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

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

using Envoy::Config::Metadata;

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

namespace {

const ProtobufWkt::Value& unspecifiedValue() { return ValueUtil::nullValue(); }

void truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  str = str.substr(0, max_length.value());
}

// Matches newline pattern in a system time format string (e.g. start time)
const std::regex& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "%[-_0^#]*[1-9]*(E|O)?n");
}
const std::regex& getNewlinePattern() { CONSTRUCT_ON_FIRST_USE(std::regex, "\n"); }

} // namespace

const std::string SubstitutionFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

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
                                  absl::string_view local_reply_body) const {
  std::string log_line;
  log_line.reserve(256);

  for (const FormatterProviderPtr& provider : providers_) {
    const auto bit = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body);
    log_line += bit.value_or(empty_value_string_);
  }

  return log_line;
}

std::string JsonFormatterImpl::format(const Http::RequestHeaderMap& request_headers,
                                      const Http::ResponseHeaderMap& response_headers,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      absl::string_view local_reply_body) const {
  const ProtobufWkt::Struct output_struct = struct_formatter_.format(
      request_headers, response_headers, response_trailers, stream_info, local_reply_body);

  const std::string log_line =
      MessageUtil::getJsonStringFromMessageOrDie(output_struct, false, true);
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
    absl::string_view local_reply_body) const {
  ASSERT(!providers.empty());
  if (providers.size() == 1) {
    const auto& provider = providers.front();
    if (preserve_types_) {
      return provider->formatValue(request_headers, response_headers, response_trailers,
                                   stream_info, local_reply_body);
    }

    if (omit_empty_values_) {
      return ValueUtil::optionalStringValue(provider->format(
          request_headers, response_headers, response_trailers, stream_info, local_reply_body));
    }

    const auto str = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body);
    return ValueUtil::stringValue(str.value_or(DefaultUnspecifiedValueString));
  }
  // Multiple providers forces string output.
  std::string str;
  for (const auto& provider : providers) {
    const auto bit = provider->format(request_headers, response_headers, response_trailers,
                                      stream_info, local_reply_body);
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
                                            absl::string_view local_reply_body) const {
  StructFormatMapVisitor visitor{
      [&](const std::vector<FormatterProviderPtr>& providers) {
        return providersCallback(providers, request_headers, response_headers, response_trailers,
                                 stream_info, local_reply_body);
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
  if (std::regex_search(main_header, getNewlinePattern()) ||
      std::regex_search(alternative_header, getNewlinePattern())) {
    throw EnvoyException("Invalid header configuration. Format string contains newline.");
  }
}

std::vector<FormatterProviderPtr> SubstitutionFormatParser::parse(const std::string& format) {
  return SubstitutionFormatParser::parse(format, {});
}

const SubstitutionFormatParser::FormatterProviderLookupTbl&
SubstitutionFormatParser::getKnownFormatters() {
  CONSTRUCT_ON_FIRST_USE(
      FormatterProviderLookupTbl,
      {{"REQ",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header,
                                                           max_length);
         }}},
       {"RESP",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseHeaderFormatter>(main_header, alternative_header,
                                                            max_length);
         }}},
       {"TRAILER",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, absl::optional<size_t>& max_length) {
           std::string main_header, alternative_header;

           parseSubcommandHeaders(format, main_header, alternative_header);

           return std::make_unique<ResponseTrailerFormatter>(main_header, alternative_header,
                                                             max_length);
         }}},
       {"LOCAL_REPLY_BODY",
        {CommandSyntaxChecker::COMMAND_ONLY,
         [](const std::string&, absl::optional<size_t>&) {
           return std::make_unique<LocalReplyBodyFormatter>();
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
       {"START_TIME",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<SystemTimeFormatter>(
               format,
               std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                     return stream_info.startTime();
                   }));
         }}},

       {"DYNAMIC_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
         }}},

       {"CLUSTER_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<ClusterMetadataFormatter>(filter_namespace, path, max_length);
         }}},
       {"UPSTREAM_METADATA",
        {CommandSyntaxChecker::PARAMS_REQUIRED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           std::string filter_namespace;
           std::vector<std::string> path;

           SubstitutionFormatParser::parseSubcommand(format, ':', filter_namespace, path);
           return std::make_unique<UpstreamHostMetadataFormatter>(filter_namespace, path,
                                                                  max_length);
         }}},
       {"FILTER_STATE",
        {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           return FilterStateFormatter::create(format, max_length, false);
         }}},
       {"UPSTREAM_FILTER_STATE",
        {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& format, const absl::optional<size_t>& max_length) {
           return FilterStateFormatter::create(format, max_length, true);
         }}},
       {"DOWNSTREAM_PEER_CERT_V_START",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<DownstreamPeerCertVStartFormatter>(format);
         }}},
       {"DOWNSTREAM_PEER_CERT_V_END",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<DownstreamPeerCertVEndFormatter>(format);
         }}},
       {"UPSTREAM_PEER_CERT_V_START",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<UpstreamPeerCertVStartFormatter>(format);
         }}},
       {"UPSTREAM_PEER_CERT_V_END",
        {CommandSyntaxChecker::PARAMS_OPTIONAL,
         [](const std::string& format, const absl::optional<size_t>&) {
           return std::make_unique<UpstreamPeerCertVEndFormatter>(format);
         }}},
       {"ENVIRONMENT",
        {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
         [](const std::string& key, absl::optional<size_t>& max_length) {
           return std::make_unique<EnvironmentFormatter>(key, max_length);
         }}}});
}

FormatterProviderPtr SubstitutionFormatParser::parseBuiltinCommand(const std::string& command,
                                                                   const std::string& subcommand,
                                                                   absl::optional<size_t>& length) {
  const FormatterProviderLookupTbl& providers = getKnownFormatters();

  auto it = providers.find(command);

  if (it == providers.end()) {
    return nullptr;
  }

  // Check flags for the command.
  CommandSyntaxChecker::verifySyntax((*it).second.first, command, subcommand, length);

  // Create a pointer to the formatter by calling a function
  // associated with formatter's name.
  return (*it).second.second(subcommand, length);
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

    auto formatter = parseBuiltinCommand(command, subcommand, max_length);
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

// StreamInfo std::string field extractor.
class StreamInfoStringFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatter::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo std::chrono_nanoseconds field extractor.
class StreamInfoDurationFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::chrono::nanoseconds>(const StreamInfo::StreamInfo&)>;

  StreamInfoDurationFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatter::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return absl::nullopt;
    }

    return fmt::format_int(millis.value()).str();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return unspecifiedValue();
    }

    return ValueUtil::numberValue(millis.value());
  }

private:
  absl::optional<int64_t> extractMillis(const StreamInfo::StreamInfo& stream_info) const {
    const auto time = field_extractor_(stream_info);
    if (time) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(time.value()).count();
    }
    return absl::nullopt;
  }

  FieldExtractor field_extractor_;
};

// StreamInfo uint64_t field extractor.
class StreamInfoUInt64FieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor = std::function<uint64_t(const StreamInfo::StreamInfo&)>;

  StreamInfoUInt64FieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatter::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    return fmt::format_int(field_extractor_(stream_info)).str();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::numberValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo Envoy::Network::Address::InstanceConstSharedPtr field extractor.
class StreamInfoAddressFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor =
      std::function<Network::Address::InstanceConstSharedPtr(const StreamInfo::StreamInfo&)>;

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withoutPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithoutPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> justPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType::JustPort);
  }

  StreamInfoAddressFieldExtractor(
      FieldExtractor f, StreamInfoFormatter::StreamInfoAddressFieldExtractionType extraction_type)
      : field_extractor_(f), extraction_type_(extraction_type) {}

  // StreamInfoFormatter::FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return absl::nullopt;
    }

    return toString(*address);
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return unspecifiedValue();
    }

    return ValueUtil::stringValue(toString(*address));
  }

private:
  std::string toString(const Network::Address::Instance& address) const {
    switch (extraction_type_) {
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithoutPort:
      return StreamInfo::Utility::formatDownstreamAddressNoPort(address);
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::JustPort:
      return StreamInfo::Utility::formatDownstreamAddressJustPort(address);
    case StreamInfoFormatter::StreamInfoAddressFieldExtractionType::WithPort:
    default:
      return address.asString();
    }
  }

  FieldExtractor field_extractor_;
  const StreamInfoFormatter::StreamInfoAddressFieldExtractionType extraction_type_;
};

// Ssl::ConnectionInfo std::string field extractor.
class StreamInfoSslConnectionInfoFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoSslConnectionInfoFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class StreamInfoUpstreamSslConnectionInfoFieldExtractor
    : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoUpstreamSslConnectionInfoFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

const StreamInfoFormatter::FieldExtractorLookupTbl& StreamInfoFormatter::getKnownFieldExtractors() {
  CONSTRUCT_ON_FIRST_USE(FieldExtractorLookupTbl,
                         {{"REQUEST_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.lastDownstreamRxByteReceived();
                                  });
                            }}},
                          {"REQUEST_TX_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.lastUpstreamTxByteSent();
                                  });
                            }}},
                          {"RESPONSE_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.firstUpstreamRxByteReceived();
                                  });
                            }}},
                          {"RESPONSE_TX_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    auto downstream = timing.lastDownstreamTxByteSent();
                                    auto upstream = timing.firstUpstreamRxByteReceived();

                                    absl::optional<std::chrono::nanoseconds> result;
                                    if (downstream && upstream) {
                                      result = downstream.value() - upstream.value();
                                    }

                                    return result;
                                  });
                            }}},
                          {"DOWNSTREAM_HANDSHAKE_DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    StreamInfo::TimingUtility timing(stream_info);
                                    return timing.downstreamHandshakeComplete();
                                  });
                            }}},
                          {"BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.bytesReceived();
                                  });
                            }}},
                          {"UPSTREAM_WIRE_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getUpstreamBytesMeter()->wireBytesReceived();
                                  });
                            }}},
                          {"UPSTREAM_HEADER_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getUpstreamBytesMeter()
                                        ->headerBytesReceived();
                                  });
                            }}},
                          {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getDownstreamBytesMeter()
                                        ->wireBytesReceived();
                                  });
                            }}},
                          {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getDownstreamBytesMeter()
                                        ->headerBytesReceived();
                                  });
                            }}},
                          {"PROTOCOL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return SubstitutionFormatUtils::protocolToString(
                                        stream_info.protocol());
                                  });
                            }}},
                          {"UPSTREAM_PROTOCOL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.upstreamInfo()
                                               ? SubstitutionFormatUtils::protocolToString(
                                                     stream_info.upstreamInfo()->upstreamProtocol())
                                               : absl::nullopt;
                                  });
                            }}},
                          {"RESPONSE_CODE",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.responseCode().value_or(0);
                                  });
                            }}},
                          {"RESPONSE_CODE_DETAILS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.responseCodeDetails();
                                  });
                            }}},
                          {"CONNECTION_TERMINATION_DETAILS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.connectionTerminationDetails();
                                  });
                            }}},
                          {"BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.bytesSent();
                                  });
                            }}},
                          {"UPSTREAM_WIRE_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getUpstreamBytesMeter()->wireBytesSent();
                                  });
                            }}},
                          {"UPSTREAM_HEADER_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getUpstreamBytesMeter()->headerBytesSent();
                                  });
                            }}},
                          {"DOWNSTREAM_WIRE_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getDownstreamBytesMeter()->wireBytesSent();
                                  });
                            }}},
                          {"DOWNSTREAM_HEADER_BYTES_SENT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.getDownstreamBytesMeter()->headerBytesSent();
                                  });
                            }}},
                          {"DURATION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoDurationFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.requestComplete();
                                  });
                            }}},
                          {"RESPONSE_FLAGS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return StreamInfo::ResponseFlagUtils::toShortString(
                                        stream_info);
                                  });
                            }}},
                          {"UPSTREAM_HOST",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_CLUSTER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    std::string upstream_cluster_name;
                                    if (stream_info.upstreamClusterInfo().has_value() &&
                                        stream_info.upstreamClusterInfo().value() != nullptr) {
                                      upstream_cluster_name = stream_info.upstreamClusterInfo()
                                                                  .value()
                                                                  ->observabilityName();
                                    }

                                    return upstream_cluster_name.empty()
                                               ? absl::nullopt
                                               : absl::make_optional<std::string>(
                                                     upstream_cluster_name);
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_LOCAL_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo().has_value()) {
                                      return stream_info.upstreamInfo()
                                          .value()
                                          .get()
                                          .upstreamLocalAddress();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                                    if (stream_info.upstreamInfo() &&
                                        stream_info.upstreamInfo()->upstreamHost()) {
                                      return stream_info.upstreamInfo()->upstreamHost()->address();
                                    }
                                    return nullptr;
                                  });
                            }}},
                          {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.attemptCount().value_or(0);
                                  });
                            }}},
                          {"UPSTREAM_TLS_CIPHER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.ciphersuiteString();
                                  });
                            }}},
                          {"UPSTREAM_TLS_VERSION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.tlsVersion();
                                  });
                            }}},
                          {"UPSTREAM_TLS_SESSION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sessionId();
                                  });
                            }}},
                          {"UPSTREAM_PEER_ISSUER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.issuerPeerCertificate();
                                  });
                            }}},
                          {"UPSTREAM_PEER_CERT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                                  });
                            }}},
                          {"UPSTREAM_PEER_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<
                                  StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().localAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider().remoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::withoutPort(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"DOWNSTREAM_DIRECT_REMOTE_PORT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return StreamInfoAddressFieldExtractor::justPort(
                                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .directRemoteAddress();
                                  });
                            }}},
                          {"CONNECTION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    return stream_info.downstreamAddressProvider()
                                        .connectionID()
                                        .value_or(0);
                                  });
                            }}},
                          {"REQUESTED_SERVER_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (!stream_info.downstreamAddressProvider()
                                             .requestedServerName()
                                             .empty()) {
                                      result = std::string(stream_info.downstreamAddressProvider()
                                                               .requestedServerName());
                                    }
                                    return result;
                                  });
                            }}},
                          {"ROUTE_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    std::string route_name = stream_info.getRouteName();
                                    if (!route_name.empty()) {
                                      result = route_name;
                                    }
                                    return result;
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_URI_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.uriSanPeerCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_URI_SAN",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return absl::StrJoin(connection_info.uriSanLocalCertificate(),
                                                         ",");
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_LOCAL_SUBJECT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.subjectLocalCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_SESSION_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sessionId();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_CIPHER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.ciphersuiteString();
                                  });
                            }}},
                          {"DOWNSTREAM_TLS_VERSION",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.tlsVersion();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_FINGERPRINT_256",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sha256PeerCertificateDigest();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_FINGERPRINT_1",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.sha1PeerCertificateDigest();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_SERIAL",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.serialNumberPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_ISSUER",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.issuerPeerCertificate();
                                  });
                            }}},
                          {"DOWNSTREAM_PEER_CERT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                  [](const Ssl::ConnectionInfo& connection_info) {
                                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                                  });
                            }}},
                          {"UPSTREAM_TRANSPORT_FAILURE_REASON",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (stream_info.upstreamInfo().has_value() &&
                                        !stream_info.upstreamInfo()
                                             .value()
                                             .get()
                                             .upstreamTransportFailureReason()
                                             .empty()) {
                                      result = stream_info.upstreamInfo()
                                                   .value()
                                                   .get()
                                                   .upstreamTransportFailureReason();
                                    }
                                    if (result) {
                                      std::replace(result->begin(), result->end(), ' ', '_');
                                    }
                                    return result;
                                  });
                            }}},
                          {"HOSTNAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              absl::optional<std::string> hostname =
                                  SubstitutionFormatUtils::getHostname();
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [hostname](const StreamInfo::StreamInfo&) { return hostname; });
                            }}},
                          {"FILTER_CHAIN_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    if (!stream_info.filterChainName().empty()) {
                                      return stream_info.filterChainName();
                                    }
                                    return absl::nullopt;
                                  });
                            }}},
                          {"VIRTUAL_CLUSTER_NAME",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    return stream_info.virtualClusterName();
                                  });
                            }}},
                          {"TLS_JA3_FINGERPRINT",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info) {
                                    absl::optional<std::string> result;
                                    if (!stream_info.downstreamAddressProvider()
                                             .ja3Hash()
                                             .empty()) {
                                      result = std::string(
                                          stream_info.downstreamAddressProvider().ja3Hash());
                                    }
                                    return result;
                                  });
                            }}},
                          {"STREAM_ID",
                           {CommandSyntaxChecker::COMMAND_ONLY,
                            [](const std::string&, const absl::optional<size_t>&) {
                              return std::make_unique<StreamInfoStringFieldExtractor>(
                                  [](const StreamInfo::StreamInfo& stream_info)
                                      -> absl::optional<std::string> {
                                    auto provider = stream_info.getStreamIdProvider();
                                    if (!provider.has_value()) {
                                      return {};
                                    }
                                    auto id = provider->toStringView();
                                    if (!id.has_value()) {
                                      return {};
                                    }
                                    return absl::make_optional<std::string>(id.value());
                                  });
                            }}}});
}

StreamInfoFormatter::StreamInfoFormatter(const std::string& command, const std::string& subcommand,
                                         const absl::optional<size_t>& length) {
  const FieldExtractorLookupTbl& extractors = getKnownFieldExtractors();

  auto it = extractors.find(command);

  if (it == extractors.end()) {
    throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", command));
  }

  // Check flags for the command.
  CommandSyntaxChecker::verifySyntax((*it).second.first, command, subcommand, length);

  // Create a pointer to the formatter by calling a function
  // associated with formatter's name.
  field_extractor_ = (*it).second.second(subcommand, length);
}

absl::optional<std::string> StreamInfoFormatter::format(const Http::RequestHeaderMap&,
                                                        const Http::ResponseHeaderMap&,
                                                        const Http::ResponseTrailerMap&,
                                                        const StreamInfo::StreamInfo& stream_info,
                                                        absl::string_view) const {
  return field_extractor_->extract(stream_info);
}

ProtobufWkt::Value StreamInfoFormatter::formatValue(const Http::RequestHeaderMap&,
                                                    const Http::ResponseHeaderMap&,
                                                    const Http::ResponseTrailerMap&,
                                                    const StreamInfo::StreamInfo& stream_info,
                                                    absl::string_view) const {
  return field_extractor_->extractValue(stream_info);
}

PlainStringFormatter::PlainStringFormatter(const std::string& str) { str_.set_string_value(str); }

absl::optional<std::string> PlainStringFormatter::format(const Http::RequestHeaderMap&,
                                                         const Http::ResponseHeaderMap&,
                                                         const Http::ResponseTrailerMap&,
                                                         const StreamInfo::StreamInfo&,
                                                         absl::string_view) const {
  return str_.string_value();
}

ProtobufWkt::Value PlainStringFormatter::formatValue(const Http::RequestHeaderMap&,
                                                     const Http::ResponseHeaderMap&,
                                                     const Http::ResponseTrailerMap&,
                                                     const StreamInfo::StreamInfo&,
                                                     absl::string_view) const {
  return str_;
}

PlainNumberFormatter::PlainNumberFormatter(double num) { num_.set_number_value(num); }

absl::optional<std::string> PlainNumberFormatter::format(const Http::RequestHeaderMap&,
                                                         const Http::ResponseHeaderMap&,
                                                         const Http::ResponseTrailerMap&,
                                                         const StreamInfo::StreamInfo&,
                                                         absl::string_view) const {
  std::string str = absl::StrFormat("%g", num_.number_value());
  return str;
}

ProtobufWkt::Value PlainNumberFormatter::formatValue(const Http::RequestHeaderMap&,
                                                     const Http::ResponseHeaderMap&,
                                                     const Http::ResponseTrailerMap&,
                                                     const StreamInfo::StreamInfo&,
                                                     absl::string_view) const {
  return num_;
}

absl::optional<std::string>
LocalReplyBodyFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                absl::string_view local_reply_body) const {
  return std::string(local_reply_body);
}

ProtobufWkt::Value LocalReplyBodyFormatter::formatValue(const Http::RequestHeaderMap&,
                                                        const Http::ResponseHeaderMap&,
                                                        const Http::ResponseTrailerMap&,
                                                        const StreamInfo::StreamInfo&,
                                                        absl::string_view local_reply_body) const {
  return ValueUtil::stringValue(std::string(local_reply_body));
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
  truncate(val, max_length_);
  return val;
}

ProtobufWkt::Value HeaderFormatter::formatValue(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return unspecifiedValue();
  }

  std::string val = std::string(header->value().getStringView());
  truncate(val, max_length_);
  return ValueUtil::stringValue(val);
}

ResponseHeaderFormatter::ResponseHeaderFormatter(const std::string& main_header,
                                                 const std::string& alternative_header,
                                                 absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string> ResponseHeaderFormatter::format(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::format(response_headers);
}

ProtobufWkt::Value ResponseHeaderFormatter::formatValue(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::formatValue(response_headers);
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
RequestHeaderFormatter::format(const Http::RequestHeaderMap& request_headers,
                               const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
                               const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::format(request_headers);
}

ProtobufWkt::Value
RequestHeaderFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                    const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
                                    const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::formatValue(request_headers);
}

ResponseTrailerFormatter::ResponseTrailerFormatter(const std::string& main_header,
                                                   const std::string& alternative_header,
                                                   absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

absl::optional<std::string>
ResponseTrailerFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::format(response_trailers);
}

ProtobufWkt::Value
ResponseTrailerFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo&, absl::string_view) const {
  return HeaderFormatter::formatValue(response_trailers);
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
HeadersByteSizeFormatter::format(const Http::RequestHeaderMap& request_headers,
                                 const Http::ResponseHeaderMap& response_headers,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo&, absl::string_view) const {
  return absl::StrCat(extractHeadersByteSize(request_headers, response_headers, response_trailers));
}

ProtobufWkt::Value
HeadersByteSizeFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                      const Http::ResponseHeaderMap& response_headers,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo&, absl::string_view) const {
  return ValueUtil::numberValue(
      extractHeadersByteSize(request_headers, response_headers, response_trailers));
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
GrpcStatusFormatter::format(const Http::RequestHeaderMap&,
                            const Http::ResponseHeaderMap& response_headers,
                            const Http::ResponseTrailerMap& response_trailers,
                            const StreamInfo::StreamInfo& info, absl::string_view) const {
  const auto grpc_status =
      Grpc::Common::getGrpcStatus(response_trailers, response_headers, info, true);
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
GrpcStatusFormatter::formatValue(const Http::RequestHeaderMap&,
                                 const Http::ResponseHeaderMap& response_headers,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo& info, absl::string_view) const {
  const auto grpc_status =
      Grpc::Common::getGrpcStatus(response_trailers, response_headers, info, true);
  if (!grpc_status.has_value()) {
    return unspecifiedValue();
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
MetadataFormatter::MetadataFormatter(const std::string& filter_namespace,
                                     const std::vector<std::string>& path,
                                     absl::optional<size_t> max_length,
                                     MetadataFormatter::GetMetadataFunction get_func)
    : filter_namespace_(filter_namespace), path_(path), max_length_(max_length),
      get_func_(get_func) {}

absl::optional<std::string>
MetadataFormatter::formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
  ProtobufWkt::Value value = formatMetadataValue(metadata);
  if (value.kind_case() == ProtobufWkt::Value::kNullValue) {
    return absl::nullopt;
  }

  std::string str;
  if (value.kind_case() == ProtobufWkt::Value::kStringValue) {
    str = value.string_value();
  } else {
    str = MessageUtil::getJsonStringFromMessageOrDie(value, false, true);
  }
  truncate(str, max_length_);
  return str;
}

ProtobufWkt::Value
MetadataFormatter::formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const {
  if (path_.empty()) {
    const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
    if (filter_it == metadata.filter_metadata().end()) {
      return unspecifiedValue();
    }
    ProtobufWkt::Value output;
    output.mutable_struct_value()->CopyFrom(filter_it->second);
    return output;
  }

  const ProtobufWkt::Value& val = Metadata::metadataValue(&metadata, filter_namespace_, path_);
  if (val.kind_case() == ProtobufWkt::Value::KindCase::KIND_NOT_SET) {
    return unspecifiedValue();
  }

  return val;
}

absl::optional<std::string> MetadataFormatter::format(const Http::RequestHeaderMap&,
                                                      const Http::ResponseHeaderMap&,
                                                      const Http::ResponseTrailerMap&,
                                                      const StreamInfo::StreamInfo& stream_info,
                                                      absl::string_view) const {
  auto metadata = get_func_(stream_info);
  return (metadata != nullptr) ? formatMetadata(*metadata) : absl::nullopt;
}

ProtobufWkt::Value MetadataFormatter::formatValue(const Http::RequestHeaderMap&,
                                                  const Http::ResponseHeaderMap&,
                                                  const Http::ResponseTrailerMap&,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  absl::string_view) const {
  auto metadata = get_func_(stream_info);
  return formatMetadataValue((metadata != nullptr) ? *metadata
                                                   : envoy::config::core::v3::Metadata());
}

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by
// @htuch. See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info) {
                          return &stream_info.dynamicMetadata();
                        }) {}

ClusterMetadataFormatter::ClusterMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info)
                            -> const envoy::config::core::v3::Metadata* {
                          auto cluster_info = stream_info.upstreamClusterInfo();
                          if (!cluster_info.has_value() || cluster_info.value() == nullptr) {
                            return nullptr;
                          }
                          return &cluster_info.value()->metadata();
                        }) {}

UpstreamHostMetadataFormatter::UpstreamHostMetadataFormatter(const std::string& filter_namespace,
                                                             const std::vector<std::string>& path,
                                                             absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info)
                            -> const envoy::config::core::v3::Metadata* {
                          if (!stream_info.upstreamInfo().has_value()) {
                            return nullptr;
                          }
                          Upstream::HostDescriptionConstSharedPtr host =
                              stream_info.upstreamInfo()->upstreamHost();
                          if (host == nullptr) {
                            return nullptr;
                          }
                          return host->metadata().get();
                        }) {}

std::unique_ptr<FilterStateFormatter>
FilterStateFormatter::create(const std::string& format, const absl::optional<size_t>& max_length,
                             bool is_upstream) {
  std::string key, serialize_type;
  static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
  static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};

  SubstitutionFormatParser::parseSubcommand(format, ':', key, serialize_type);
  if (key.empty()) {
    throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
  }

  if (serialize_type.empty()) {
    serialize_type = std::string(TYPED_SERIALIZATION);
  }
  if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION) {
    throw EnvoyException("Invalid filter state serialize type, only "
                         "support PLAIN/TYPED.");
  }

  const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

  return std::make_unique<FilterStateFormatter>(key, max_length, serialize_as_string, is_upstream);
}

FilterStateFormatter::FilterStateFormatter(const std::string& key,
                                           absl::optional<size_t> max_length,
                                           bool serialize_as_string, bool is_upstream)
    : key_(key), max_length_(max_length), serialize_as_string_(serialize_as_string),
      is_upstream_(is_upstream) {}

const Envoy::StreamInfo::FilterState::Object*
FilterStateFormatter::filterState(const StreamInfo::StreamInfo& stream_info) const {
  const StreamInfo::FilterState* filter_state = nullptr;
  if (is_upstream_) {
    const OptRef<const StreamInfo::UpstreamInfo> upstream_info = stream_info.upstreamInfo();
    if (upstream_info) {
      filter_state = upstream_info->upstreamFilterState().get();
    }
  } else {
    filter_state = &stream_info.filterState();
  }

  if (filter_state) {
    return filter_state->getDataReadOnly<StreamInfo::FilterState::Object>(key_);
  }

  return nullptr;
}

absl::optional<std::string> FilterStateFormatter::format(const Http::RequestHeaderMap&,
                                                         const Http::ResponseHeaderMap&,
                                                         const Http::ResponseTrailerMap&,
                                                         const StreamInfo::StreamInfo& stream_info,
                                                         absl::string_view) const {
  const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
  if (!state) {
    return absl::nullopt;
  }

  if (serialize_as_string_) {
    absl::optional<std::string> plain_value = state->serializeAsString();
    if (plain_value.has_value()) {
      truncate(plain_value.value(), max_length_);
      return plain_value.value();
    }
    return absl::nullopt;
  }

  ProtobufTypes::MessagePtr proto = state->serializeAsProto();
  if (proto == nullptr) {
    return absl::nullopt;
  }

  std::string value;
  const auto status = Protobuf::util::MessageToJsonString(*proto, &value);
  if (!status.ok()) {
    // If the message contains an unknown Any (from WASM or Lua), MessageToJsonString will fail.
    // TODO(lizan): add support of unknown Any.
    return absl::nullopt;
  }

  truncate(value, max_length_);
  return value;
}

ProtobufWkt::Value FilterStateFormatter::formatValue(const Http::RequestHeaderMap&,
                                                     const Http::ResponseHeaderMap&,
                                                     const Http::ResponseTrailerMap&,
                                                     const StreamInfo::StreamInfo& stream_info,
                                                     absl::string_view) const {
  const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
  if (!state) {
    return unspecifiedValue();
  }

  if (serialize_as_string_) {
    absl::optional<std::string> plain_value = state->serializeAsString();
    if (plain_value.has_value()) {
      truncate(plain_value.value(), max_length_);
      return ValueUtil::stringValue(plain_value.value());
    }
    return unspecifiedValue();
  }

  ProtobufTypes::MessagePtr proto = state->serializeAsProto();
  if (!proto) {
    return unspecifiedValue();
  }

  ProtobufWkt::Value val;
  if (MessageUtil::jsonConvertValue(*proto, val)) {
    return val;
  }
  return unspecifiedValue();
}

// A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
// an access log command that starts with `START_TIME`.
StartTimeFormatter::StartTimeFormatter(const std::string& format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      })) {}

DownstreamPeerCertVStartFormatter::DownstreamPeerCertVStartFormatter(const std::string& format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->validFromPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
DownstreamPeerCertVEndFormatter::DownstreamPeerCertVEndFormatter(const std::string& format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->expirationPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
UpstreamPeerCertVStartFormatter::UpstreamPeerCertVStartFormatter(const std::string& format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.upstreamInfo() &&
                                       stream_info.upstreamInfo()->upstreamSslConnection() !=
                                           nullptr
                                   ? stream_info.upstreamInfo()
                                         ->upstreamSslConnection()
                                         ->validFromPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
UpstreamPeerCertVEndFormatter::UpstreamPeerCertVEndFormatter(const std::string& format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.upstreamInfo() &&
                                       stream_info.upstreamInfo()->upstreamSslConnection() !=
                                           nullptr
                                   ? stream_info.upstreamInfo()
                                         ->upstreamSslConnection()
                                         ->expirationPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}

SystemTimeFormatter::SystemTimeFormatter(const std::string& format, TimeFieldExtractorPtr f)
    : date_formatter_(format), time_field_extractor_(std::move(f)) {
  // Validate the input specifier here. The formatted string may be destined for a header, and
  // should not contain invalid characters {NUL, LR, CF}.
  if (std::regex_search(format, getSystemTimeFormatNewlinePattern())) {
    throw EnvoyException("Invalid header configuration. Format string contains newline.");
  }
}

absl::optional<std::string> SystemTimeFormatter::format(const Http::RequestHeaderMap&,
                                                        const Http::ResponseHeaderMap&,
                                                        const Http::ResponseTrailerMap&,
                                                        const StreamInfo::StreamInfo& stream_info,
                                                        absl::string_view) const {
  const auto time_field = (*time_field_extractor_)(stream_info);
  if (!time_field.has_value()) {
    return absl::nullopt;
  }
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(time_field.value());
  }
  return date_formatter_.fromTime(time_field.value());
}

ProtobufWkt::Value SystemTimeFormatter::formatValue(
    const Http::RequestHeaderMap& request_headers, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap& response_trailers, const StreamInfo::StreamInfo& stream_info,
    absl::string_view local_reply_body) const {
  return ValueUtil::optionalStringValue(
      format(request_headers, response_headers, response_trailers, stream_info, local_reply_body));
}

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

EnvironmentFormatter::EnvironmentFormatter(const std::string& key,
                                           absl::optional<size_t> max_length) {
  ASSERT(!key.empty());

  const char* env_value = std::getenv(key.c_str());
  if (env_value != nullptr) {
    std::string env_string = env_value;
    truncate(env_string, max_length);
    str_.set_string_value(env_string);
    return;
  }
  str_.set_string_value(DefaultUnspecifiedValueString);
}

absl::optional<std::string> EnvironmentFormatter::format(const Http::RequestHeaderMap&,
                                                         const Http::ResponseHeaderMap&,
                                                         const Http::ResponseTrailerMap&,
                                                         const StreamInfo::StreamInfo&,
                                                         absl::string_view) const {
  return str_.string_value();
}
ProtobufWkt::Value EnvironmentFormatter::formatValue(const Http::RequestHeaderMap&,
                                                     const Http::ResponseHeaderMap&,
                                                     const Http::ResponseTrailerMap&,
                                                     const StreamInfo::StreamInfo&,
                                                     absl::string_view) const {
  return str_;
}

} // namespace Formatter
} // namespace Envoy
