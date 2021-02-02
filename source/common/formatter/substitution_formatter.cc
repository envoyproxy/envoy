#include "common/formatter/substitution_formatter.h"

#include <climits>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"
#include "common/stream_info/utility.h"

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
  return FormatterPtr{new FormatterImpl(DEFAULT_FORMAT, false)};
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
  if (result.rc_ == 0) {
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
                                 bool omit_empty_values)
    : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
      empty_value_(omit_empty_values_ ? EMPTY_STRING : DefaultUnspecifiedValueString),
      struct_output_format_(toFormatMapValue(format_mapping)) {}

StructFormatter::StructFormatMapWrapper
StructFormatter::toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
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

    default:
      throw EnvoyException("Only string values, nested structs and list values are "
                           "supported in structured access log format.");
    }
  }
  return {std::move(output)};
};

StructFormatter::StructFormatListWrapper
StructFormatter::toFormatListValue(const ProtobufWkt::ListValue& list_value_format) const {
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
    default:
      throw EnvoyException("Only string values, nested structs and list values are "
                           "supported in structured access log format.");
    }
  }
  return {std::move(output)};
}

std::vector<FormatterProviderPtr>
StructFormatter::toFormatStringValue(const std::string& string_format) const {
  return SubstitutionFormatParser::parse(string_format);
};

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

void SubstitutionFormatParser::parseCommandHeader(const std::string& token, const size_t start,
                                                  std::string& main_header,
                                                  std::string& alternative_header,
                                                  absl::optional<size_t>& max_length) {
  std::vector<std::string> subs;
  parseCommand(token, start, "?", main_header, subs, max_length);
  if (subs.size() > 1) {
    throw EnvoyException(
        // Header format rules support only one alternative header.
        // docs/root/configuration/access_log.rst#format-rules
        absl::StrCat("More than 1 alternative header specified in token: ", token));
  }
  if (subs.size() == 1) {
    alternative_header = subs.front();
  } else {
    alternative_header = "";
  }
  // The main and alternative header should not contain invalid characters {NUL, LR, CF}.
  if (std::regex_search(main_header, getNewlinePattern()) ||
      std::regex_search(alternative_header, getNewlinePattern())) {
    throw EnvoyException("Invalid header configuration. Format string contains newline.");
  }
}

void SubstitutionFormatParser::parseCommand(const std::string& token, const size_t start,
                                            const std::string& separator, std::string& main,
                                            std::vector<std::string>& sub_items,
                                            absl::optional<size_t>& max_length) {
  // TODO(dnoe): Convert this to use string_view throughout.
  const size_t end_request = token.find(')', start);
  sub_items.clear();
  if (end_request != token.length() - 1) {
    // Closing bracket is not found.
    if (end_request == std::string::npos) {
      throw EnvoyException(absl::StrCat("Closing bracket is missing in token: ", token));
    }

    // Closing bracket should be either last one or followed by ':' to denote limitation.
    if (token[end_request + 1] != ':') {
      throw EnvoyException(absl::StrCat("Incorrect position of ')' in token: ", token));
    }

    const auto length_str = absl::string_view(token).substr(end_request + 2);
    uint64_t length_value;

    if (!absl::SimpleAtoi(length_str, &length_value)) {
      throw EnvoyException(absl::StrCat("Length must be an integer, given: ", length_str));
    }

    max_length = length_value;
  }

  const std::string name_data = token.substr(start, end_request - start);
  if (!separator.empty()) {
    const std::vector<std::string> keys = absl::StrSplit(name_data, separator);
    if (!keys.empty()) {
      // The main value is the first key
      main = keys.at(0);
      if (keys.size() > 1) {
        // Sub items contain additional keys
        sub_items.insert(sub_items.end(), keys.begin() + 1, keys.end());
      }
    }
  } else {
    main = name_data;
  }
}

std::vector<FormatterProviderPtr> SubstitutionFormatParser::parse(const std::string& format) {
  return SubstitutionFormatParser::parse(format, {});
}

FormatterProviderPtr SubstitutionFormatParser::parseBuiltinCommand(const std::string& token) {
  static constexpr absl::string_view DYNAMIC_META_TOKEN{"DYNAMIC_METADATA("};
  static constexpr absl::string_view FILTER_STATE_TOKEN{"FILTER_STATE("};
  static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
  static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};

  if (absl::StartsWith(token, "REQ(")) {
    std::string main_header, alternative_header;
    absl::optional<size_t> max_length;

    parseCommandHeader(token, ReqParamStart, main_header, alternative_header, max_length);

    return std::make_unique<RequestHeaderFormatter>(main_header, alternative_header, max_length);
  } else if (absl::StartsWith(token, "RESP(")) {
    std::string main_header, alternative_header;
    absl::optional<size_t> max_length;

    parseCommandHeader(token, RespParamStart, main_header, alternative_header, max_length);

    return std::make_unique<ResponseHeaderFormatter>(main_header, alternative_header, max_length);
  } else if (absl::StartsWith(token, "TRAILER(")) {
    std::string main_header, alternative_header;
    absl::optional<size_t> max_length;

    parseCommandHeader(token, TrailParamStart, main_header, alternative_header, max_length);

    return std::make_unique<ResponseTrailerFormatter>(main_header, alternative_header, max_length);
  } else if (absl::StartsWith(token, "LOCAL_REPLY_BODY")) {
    return std::make_unique<LocalReplyBodyFormatter>();
  } else if (absl::StartsWith(token, DYNAMIC_META_TOKEN)) {
    std::string filter_namespace;
    absl::optional<size_t> max_length;
    std::vector<std::string> path;
    const size_t start = DYNAMIC_META_TOKEN.size();

    parseCommand(token, start, ":", filter_namespace, path, max_length);
    return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
  } else if (absl::StartsWith(token, FILTER_STATE_TOKEN)) {
    std::string key;
    absl::optional<size_t> max_length;
    std::vector<std::string> path;
    const size_t start = FILTER_STATE_TOKEN.size();

    parseCommand(token, start, ":", key, path, max_length);
    if (key.empty()) {
      throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
    }

    const absl::string_view serialize_type =
        !path.empty() ? path[path.size() - 1] : TYPED_SERIALIZATION;

    if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION) {
      throw EnvoyException("Invalid filter state serialize type, only support PLAIN/TYPED.");
    }
    const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

    return std::make_unique<FilterStateFormatter>(key, max_length, serialize_as_string);
  } else if (absl::StartsWith(token, "START_TIME")) {
    return std::make_unique<StartTimeFormatter>(token);
  } else if (absl::StartsWith(token, "DOWNSTREAM_PEER_CERT_V_START")) {
    return std::make_unique<DownstreamPeerCertVStartFormatter>(token);
  } else if (absl::StartsWith(token, "DOWNSTREAM_PEER_CERT_V_END")) {
    return std::make_unique<DownstreamPeerCertVEndFormatter>(token);
  } else if (absl::StartsWith(token, "GRPC_STATUS")) {
    return std::make_unique<GrpcStatusFormatter>("grpc-status", "", absl::optional<size_t>());
  } else if (absl::StartsWith(token, "REQUEST_HEADERS_BYTES")) {
    return std::make_unique<HeadersByteSizeFormatter>(
        HeadersByteSizeFormatter::HeaderType::RequestHeaders);
  } else if (absl::StartsWith(token, "RESPONSE_HEADERS_BYTES")) {
    return std::make_unique<HeadersByteSizeFormatter>(
        HeadersByteSizeFormatter::HeaderType::ResponseHeaders);
  } else if (absl::StartsWith(token, "RESPONSE_TRAILERS_BYTES")) {
    return std::make_unique<HeadersByteSizeFormatter>(
        HeadersByteSizeFormatter::HeaderType::ResponseTrailers);
  }

  return nullptr;
}

// TODO(derekargueta): #2967 - Rewrite SubstitutionFormatter with parser library & formal grammar
std::vector<FormatterProviderPtr>
SubstitutionFormatParser::parse(const std::string& format,
                                const std::vector<CommandParserPtr>& commands) {
  std::string current_token;
  std::vector<FormatterProviderPtr> formatters;
  const std::regex command_w_args_regex(R"EOF(^%([A-Z]|_)+(\([^\)]*\))?(:[0-9]+)?(%))EOF");

  for (size_t pos = 0; pos < format.length(); ++pos) {
    if (format[pos] != '%') {
      current_token += format[pos];
      continue;
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
    const std::string token = match.substr(1, match.length() - 2);
    pos += 1;
    const size_t command_end_position = pos + token.length();

    auto formatter = parseBuiltinCommand(token);
    if (formatter) {
      formatters.push_back(std::move(formatter));
    } else {
      // Check formatter extensions. These are used for anything not provided by the built-in
      // operators, e.g.: specialized formatting, computing stats from request/response headers
      // or from stream info, etc.
      bool added = false;
      for (const auto& cmd : commands) {
        auto formatter = cmd->parse(token, pos, command_end_position);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      if (!added) {
        formatters.emplace_back(FormatterProviderPtr{new StreamInfoFormatter(token)});
      }
    }

    pos = command_end_position;
  }

  if (!current_token.empty()) {
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
    if (stream_info.downstreamSslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*stream_info.downstreamSslConnection());
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamSslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamSslConnection());
    if (value && value->empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

StreamInfoFormatter::StreamInfoFormatter(const std::string& field_name) {
  if (field_name == "REQUEST_DURATION") {
    field_extractor_ = std::make_unique<StreamInfoDurationFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.lastDownstreamRxByteReceived();
        });
  } else if (field_name == "RESPONSE_DURATION") {
    field_extractor_ = std::make_unique<StreamInfoDurationFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.firstUpstreamRxByteReceived();
        });
  } else if (field_name == "RESPONSE_TX_DURATION") {
    field_extractor_ = std::make_unique<StreamInfoDurationFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          auto downstream = stream_info.lastDownstreamTxByteSent();
          auto upstream = stream_info.firstUpstreamRxByteReceived();

          absl::optional<std::chrono::nanoseconds> result;
          if (downstream && upstream) {
            result = downstream.value() - upstream.value();
          }

          return result;
        });
  } else if (field_name == "BYTES_RECEIVED") {
    field_extractor_ = std::make_unique<StreamInfoUInt64FieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) { return stream_info.bytesReceived(); });
  } else if (field_name == "PROTOCOL") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return SubstitutionFormatUtils::protocolToString(stream_info.protocol());
        });
  } else if (field_name == "RESPONSE_CODE") {
    field_extractor_ = std::make_unique<StreamInfoUInt64FieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.responseCode().value_or(0);
        });
  } else if (field_name == "RESPONSE_CODE_DETAILS") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.responseCodeDetails();
        });
  } else if (field_name == "CONNECTION_TERMINATION_DETAILS") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.connectionTerminationDetails();
        });
  } else if (field_name == "BYTES_SENT") {
    field_extractor_ = std::make_unique<StreamInfoUInt64FieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) { return stream_info.bytesSent(); });
  } else if (field_name == "DURATION") {
    field_extractor_ = std::make_unique<StreamInfoDurationFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) { return stream_info.requestComplete(); });
  } else if (field_name == "RESPONSE_FLAGS") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
        });
  } else if (field_name == "UPSTREAM_HOST") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.upstreamHost() ? stream_info.upstreamHost()->address() : nullptr;
        });
  } else if (field_name == "UPSTREAM_CLUSTER") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          std::string upstream_cluster_name;
          if (nullptr != stream_info.upstreamHost()) {
            upstream_cluster_name = stream_info.upstreamHost()->cluster().name();
          }

          return upstream_cluster_name.empty()
                     ? absl::nullopt
                     : absl::make_optional<std::string>(upstream_cluster_name);
        });
  } else if (field_name == "UPSTREAM_LOCAL_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.upstreamLocalAddress();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().localAddress();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = StreamInfoAddressFieldExtractor::withoutPort(
        [](const Envoy::StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().localAddress();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_PORT") {
    field_extractor_ = StreamInfoAddressFieldExtractor::justPort(
        [](const Envoy::StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().localAddress();
        });
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().remoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withoutPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().remoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().directRemoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withoutPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamAddressProvider().directRemoteAddress();
        });
  } else if (field_name == "CONNECTION_ID") {
    field_extractor_ = std::make_unique<StreamInfoUInt64FieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.connectionID().value_or(0);
        });
  } else if (field_name == "REQUESTED_SERVER_NAME") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          absl::optional<std::string> result;
          if (!stream_info.requestedServerName().empty()) {
            result = stream_info.requestedServerName();
          }
          return result;
        });
  } else if (field_name == "ROUTE_NAME") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          absl::optional<std::string> result;
          std::string route_name = stream_info.getRouteName();
          if (!route_name.empty()) {
            result = route_name;
          }
          return result;
        });
  } else if (field_name == "DOWNSTREAM_PEER_URI_SAN") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_URI_SAN") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
        });
  } else if (field_name == "DOWNSTREAM_PEER_SUBJECT") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.subjectPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_SUBJECT") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.subjectLocalCertificate();
        });
  } else if (field_name == "DOWNSTREAM_TLS_SESSION_ID") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) { return connection_info.sessionId(); });
  } else if (field_name == "DOWNSTREAM_TLS_CIPHER") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.ciphersuiteString();
        });
  } else if (field_name == "DOWNSTREAM_TLS_VERSION") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) { return connection_info.tlsVersion(); });
  } else if (field_name == "DOWNSTREAM_PEER_FINGERPRINT_256") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.sha256PeerCertificateDigest();
        });
  } else if (field_name == "DOWNSTREAM_PEER_FINGERPRINT_1") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.sha1PeerCertificateDigest();
        });
  } else if (field_name == "DOWNSTREAM_PEER_SERIAL") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.serialNumberPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_ISSUER") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.issuerPeerCertificate();
        });
  } else if (field_name == "DOWNSTREAM_PEER_CERT") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          return connection_info.urlEncodedPemEncodedPeerCertificate();
        });
  } else if (field_name == "UPSTREAM_TRANSPORT_FAILURE_REASON") {
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          absl::optional<std::string> result;
          if (!stream_info.upstreamTransportFailureReason().empty()) {
            result = stream_info.upstreamTransportFailureReason();
          }
          return result;
        });
  } else if (field_name == "HOSTNAME") {
    absl::optional<std::string> hostname = SubstitutionFormatUtils::getHostname();
    field_extractor_ = std::make_unique<StreamInfoStringFieldExtractor>(
        [hostname](const StreamInfo::StreamInfo&) { return hostname; });
  } else {
    throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", field_name));
  }
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
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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

GrpcStatusFormatter::GrpcStatusFormatter(const std::string& main_header,
                                         const std::string& alternative_header,
                                         absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

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
  const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
  if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
    return std::to_string(grpc_status.value());
  }
  return grpc_status_message;
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
  const auto grpc_status_message = Grpc::Utility::grpcStatusToString(grpc_status.value());
  if (grpc_status_message == EMPTY_STRING || grpc_status_message == "InvalidCode") {
    return ValueUtil::stringValue(std::to_string(grpc_status.value()));
  }
  return ValueUtil::stringValue(grpc_status_message);
}

MetadataFormatter::MetadataFormatter(const std::string& filter_namespace,
                                     const std::vector<std::string>& path,
                                     absl::optional<size_t> max_length)
    : filter_namespace_(filter_namespace), path_(path), max_length_(max_length) {}

absl::optional<std::string>
MetadataFormatter::formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
  ProtobufWkt::Value value = formatMetadataValue(metadata);
  if (value.kind_case() == ProtobufWkt::Value::kNullValue) {
    return absl::nullopt;
  }

  std::string json = MessageUtil::getJsonStringFromMessageOrDie(value, false, true);
  truncate(json, max_length_);
  return json;
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

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by
// @htuch. See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length) {}

absl::optional<std::string> DynamicMetadataFormatter::format(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
    const StreamInfo::StreamInfo& stream_info, absl::string_view) const {
  return MetadataFormatter::formatMetadata(stream_info.dynamicMetadata());
}

ProtobufWkt::Value DynamicMetadataFormatter::formatValue(const Http::RequestHeaderMap&,
                                                         const Http::ResponseHeaderMap&,
                                                         const Http::ResponseTrailerMap&,
                                                         const StreamInfo::StreamInfo& stream_info,
                                                         absl::string_view) const {
  return MetadataFormatter::formatMetadataValue(stream_info.dynamicMetadata());
}

FilterStateFormatter::FilterStateFormatter(const std::string& key,
                                           absl::optional<size_t> max_length,
                                           bool serialize_as_string)
    : key_(key), max_length_(max_length), serialize_as_string_(serialize_as_string) {}

const Envoy::StreamInfo::FilterState::Object*
FilterStateFormatter::filterState(const StreamInfo::StreamInfo& stream_info) const {
  const StreamInfo::FilterState& filter_state = stream_info.filterState();
  if (!filter_state.hasDataWithName(key_)) {
    return nullptr;
  }
  return &filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(key_);
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
  try {
    MessageUtil::jsonConvertValue(*proto, val);
  } catch (EnvoyException& ex) {
    return unspecifiedValue();
  }
  return val;
}

// Given a token, extract the command string between parenthesis if it exists.
std::string SystemTimeFormatter::parseFormat(const std::string& token, size_t parameters_start) {
  const size_t parameters_length = token.length() - (parameters_start + 1);
  return token[parameters_start - 1] == '(' ? token.substr(parameters_start, parameters_length)
                                            : "";
}

// A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
// an access log token that starts with `START_TIME`.
StartTimeFormatter::StartTimeFormatter(const std::string& token)
    : SystemTimeFormatter(
          parseFormat(token, sizeof("START_TIME(") - 1),
          std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
              [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                return stream_info.startTime();
              })) {}

// A SystemTime formatter that optionally extracts the start date from the downstream peer's
// certificate. Must be provided an access log token that starts with `DOWNSTREAM_PEER_CERT_V_START`
DownstreamPeerCertVStartFormatter::DownstreamPeerCertVStartFormatter(const std::string& token)
    : SystemTimeFormatter(
          parseFormat(token, sizeof("DOWNSTREAM_PEER_CERT_V_START(") - 1),
          std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
              [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                const auto connection_info = stream_info.downstreamSslConnection();
                return connection_info != nullptr ? connection_info->validFromPeerCertificate()
                                                  : absl::optional<SystemTime>();
              })) {}

// A SystemTime formatter that optionally extracts the end date from the downstream peer's
// certificate. Must be provided an access log token that starts with `DOWNSTREAM_PEER_CERT_V_END`
DownstreamPeerCertVEndFormatter::DownstreamPeerCertVEndFormatter(const std::string& token)
    : SystemTimeFormatter(
          parseFormat(token, sizeof("DOWNSTREAM_PEER_CERT_V_END(") - 1),
          std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
              [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                const auto connection_info = stream_info.downstreamSslConnection();
                return connection_info != nullptr ? connection_info->expirationPeerCertificate()
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

} // namespace Formatter
} // namespace Envoy
