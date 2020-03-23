#include "common/access_log/access_log_formatter.h"

#include <limits.h>

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
#include "common/http/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"
#include "common/stream_info/utility.h"

#include "absl/strings/str_split.h"
#include "fmt/format.h"

using Envoy::Config::Metadata;

namespace Envoy {
namespace AccessLog {

static const std::string UnspecifiedValueString = "-";

namespace {

const ProtobufWkt::Value& unspecifiedValue() { return ValueUtil::nullValue(); }

void truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  str = str.substr(0, max_length.value());
}

// Matches newline pattern in a StartTimeFormatter format string.
const std::regex& getStartTimeNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "%[-_0^#]*[1-9]*n");
}
const std::regex& getNewlinePattern() { CONSTRUCT_ON_FIRST_USE(std::regex, "\n"); }

} // namespace

const std::string AccessLogFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

FormatterPtr AccessLogFormatUtils::defaultAccessLogFormatter() {
  return FormatterPtr{new FormatterImpl(DEFAULT_FORMAT)};
}

const std::string&
AccessLogFormatUtils::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return UnspecifiedValueString;
}

const std::string AccessLogFormatUtils::getHostname() {
#ifdef HOST_NAME_MAX
  const size_t len = HOST_NAME_MAX;
#else
  // This is notably the case in OSX.
  const size_t len = 255;
#endif
  char name[len];
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result = os_sys_calls.gethostname(name, len);

  std::string hostname = "-";
  if (result.rc_ == 0) {
    hostname.assign(name);
  }

  return hostname;
}

FormatterImpl::FormatterImpl(const std::string& format) {
  providers_ = AccessLogFormatParser::parse(format);
}

std::string FormatterImpl::format(const Http::RequestHeaderMap& request_headers,
                                  const Http::ResponseHeaderMap& response_headers,
                                  const Http::ResponseTrailerMap& response_trailers,
                                  const StreamInfo::StreamInfo& stream_info) const {
  std::string log_line;
  log_line.reserve(256);

  for (const FormatterProviderPtr& provider : providers_) {
    log_line += provider->format(request_headers, response_headers, response_trailers, stream_info);
  }

  return log_line;
}

JsonFormatterImpl::JsonFormatterImpl(std::unordered_map<std::string, std::string>& format_mapping,
                                     bool preserve_types)
    : preserve_types_(preserve_types) {
  for (const auto& pair : format_mapping) {
    json_output_format_.emplace(pair.first, AccessLogFormatParser::parse(pair.second));
  }
}

std::string JsonFormatterImpl::format(const Http::RequestHeaderMap& request_headers,
                                      const Http::ResponseHeaderMap& response_headers,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo& stream_info) const {
  const auto output_struct =
      toStruct(request_headers, response_headers, response_trailers, stream_info);

  const std::string log_line = MessageUtil::getJsonStringFromMessage(output_struct, false, true);
  return absl::StrCat(log_line, "\n");
}

ProtobufWkt::Struct JsonFormatterImpl::toStruct(const Http::RequestHeaderMap& request_headers,
                                                const Http::ResponseHeaderMap& response_headers,
                                                const Http::ResponseTrailerMap& response_trailers,
                                                const StreamInfo::StreamInfo& stream_info) const {
  ProtobufWkt::Struct output;
  auto* fields = output.mutable_fields();
  for (const auto& pair : json_output_format_) {
    const auto& providers = pair.second;
    ASSERT(!providers.empty());

    if (providers.size() == 1) {
      const auto& provider = providers.front();
      const auto val =
          preserve_types_ ? provider->formatValue(request_headers, response_headers,
                                                  response_trailers, stream_info)
                          : ValueUtil::stringValue(provider->format(
                                request_headers, response_headers, response_trailers, stream_info));

      (*fields)[pair.first] = val;
    } else {
      // Multiple providers forces string output.
      std::string str;
      for (const auto& provider : providers) {
        str += provider->format(request_headers, response_headers, response_trailers, stream_info);
      }
      (*fields)[pair.first] = ValueUtil::stringValue(str);
    }
  }
  return output;
}

void AccessLogFormatParser::parseCommandHeader(const std::string& token, const size_t start,
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

void AccessLogFormatParser::parseCommand(const std::string& token, const size_t start,
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

// TODO(derekargueta): #2967 - Rewrite AccessLogFormatter with parser library & formal grammar
std::vector<FormatterProviderPtr> AccessLogFormatParser::parse(const std::string& format) {
  std::string current_token;
  std::vector<FormatterProviderPtr> formatters;
  static constexpr absl::string_view DYNAMIC_META_TOKEN{"DYNAMIC_METADATA("};
  static constexpr absl::string_view FILTER_STATE_TOKEN{"FILTER_STATE("};
  const std::regex command_w_args_regex(R"EOF(%([A-Z]|_)+(\([^\)]*\))?(:[0-9]+)?(%))EOF");

  for (size_t pos = 0; pos < format.length(); ++pos) {
    if (format[pos] == '%') {
      if (!current_token.empty()) {
        formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
        current_token = "";
      }

      std::smatch m;
      const std::string search_space = format.substr(pos);
      if (!(std::regex_search(search_space, m, command_w_args_regex) || m.position() == 0)) {
        throw EnvoyException(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      const std::string match = m.str(0);
      const std::string token = match.substr(1, match.length() - 2);
      pos += 1;
      const int command_end_position = pos + token.length();

      if (absl::StartsWith(token, "REQ(")) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, ReqParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new RequestHeaderFormatter(main_header, alternative_header, max_length)});
      } else if (absl::StartsWith(token, "RESP(")) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, RespParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new ResponseHeaderFormatter(main_header, alternative_header, max_length)});
      } else if (absl::StartsWith(token, "TRAILER(")) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, TrailParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new ResponseTrailerFormatter(main_header, alternative_header, max_length)});
      } else if (absl::StartsWith(token, DYNAMIC_META_TOKEN)) {
        std::string filter_namespace;
        absl::optional<size_t> max_length;
        std::vector<std::string> path;
        const size_t start = DYNAMIC_META_TOKEN.size();

        parseCommand(token, start, ":", filter_namespace, path, max_length);
        formatters.emplace_back(
            FormatterProviderPtr{new DynamicMetadataFormatter(filter_namespace, path, max_length)});
      } else if (absl::StartsWith(token, FILTER_STATE_TOKEN)) {
        std::string key;
        absl::optional<size_t> max_length;
        std::vector<std::string> path;
        const size_t start = FILTER_STATE_TOKEN.size();

        parseCommand(token, start, "", key, path, max_length);
        if (key.empty()) {
          throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
        }

        formatters.push_back(std::make_unique<FilterStateFormatter>(key, max_length));
      } else if (absl::StartsWith(token, "START_TIME")) {
        const size_t parameters_length = pos + StartTimeParamStart + 1;
        const size_t parameters_end = command_end_position - parameters_length;

        const std::string args = token[StartTimeParamStart - 1] == '('
                                     ? token.substr(StartTimeParamStart, parameters_end)
                                     : "";
        // Validate the input specifier here. The formatted string may be destined for a header, and
        // should not contain invalid characters {NUL, LR, CF}.
        if (std::regex_search(args, getStartTimeNewlinePattern())) {
          throw EnvoyException("Invalid header configuration. Format string contains newline.");
        }
        formatters.emplace_back(FormatterProviderPtr{new StartTimeFormatter(args)});
      } else {
        formatters.emplace_back(FormatterProviderPtr{new StreamInfoFormatter(token)});
      }
      pos = command_end_position;
    } else {
      current_token += format[pos];
    }
  }

  if (!current_token.empty()) {
    formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

// StreamInfo std::string field extractor.
class StreamInfoStringFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor = std::function<std::string(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatter::FieldExtractor
  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::stringValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo absl::optional<std::string> field extractor.
class StreamInfoOptionalStringFieldExtractor : public StreamInfoFormatter::FieldExtractor {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoOptionalStringFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatter::FieldExtractor
  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto str = field_extractor_(stream_info);
    if (!str) {
      return UnspecifiedValueString;
    }

    return str.value();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    const auto str = field_extractor_(stream_info);
    if (!str) {
      return unspecifiedValue();
    }

    return ValueUtil::stringValue(str.value());
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
  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return UnspecifiedValueString;
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
  absl::optional<uint32_t> extractMillis(const StreamInfo::StreamInfo& stream_info) const {
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
  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
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
  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return UnspecifiedValueString;
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
  using FieldExtractor = std::function<std::string(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoSslConnectionInfoFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  std::string extract(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamSslConnection() == nullptr) {
      return UnspecifiedValueString;
    }

    const auto value = field_extractor_(*stream_info.downstreamSslConnection());
    if (value.empty()) {
      return UnspecifiedValueString;
    }

    return value;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamSslConnection() == nullptr) {
      return unspecifiedValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamSslConnection());
    if (value.empty()) {
      return unspecifiedValue();
    }

    return ValueUtil::stringValue(value);
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
          return AccessLogFormatUtils::protocolToString(stream_info.protocol());
        });
  } else if (field_name == "RESPONSE_CODE") {
    field_extractor_ = std::make_unique<StreamInfoUInt64FieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.responseCode() ? stream_info.responseCode().value() : 0;
        });
  } else if (field_name == "RESPONSE_CODE_DETAILS") {
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.responseCodeDetails();
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
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
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
          return stream_info.downstreamLocalAddress();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = StreamInfoAddressFieldExtractor::withoutPort(
        [](const Envoy::StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamLocalAddress();
        });
  } else if (field_name == "DOWNSTREAM_LOCAL_PORT") {
    field_extractor_ = StreamInfoAddressFieldExtractor::justPort(
        [](const Envoy::StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamLocalAddress();
        });
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamRemoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withoutPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamRemoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamDirectRemoteAddress();
        });
  } else if (field_name == "DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ =
        StreamInfoAddressFieldExtractor::withoutPort([](const StreamInfo::StreamInfo& stream_info) {
          return stream_info.downstreamDirectRemoteAddress();
        });
  } else if (field_name == "REQUESTED_SERVER_NAME") {
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          absl::optional<std::string> result;
          if (!stream_info.requestedServerName().empty()) {
            result = stream_info.requestedServerName();
          }
          return result;
        });
  } else if (field_name == "ROUTE_NAME") {
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
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
  } else if (field_name == "DOWNSTREAM_PEER_CERT_V_START") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          absl::optional<SystemTime> time = connection_info.validFromPeerCertificate();
          if (!time.has_value()) {
            return EMPTY_STRING;
          }
          return AccessLogDateTimeFormatter::fromTime(time.value());
        });
  } else if (field_name == "DOWNSTREAM_PEER_CERT_V_END") {
    field_extractor_ = std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
        [](const Ssl::ConnectionInfo& connection_info) {
          absl::optional<SystemTime> time = connection_info.expirationPeerCertificate();
          if (!time.has_value()) {
            return EMPTY_STRING;
          }
          return AccessLogDateTimeFormatter::fromTime(time.value());
        });
  } else if (field_name == "UPSTREAM_TRANSPORT_FAILURE_REASON") {
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
        [](const StreamInfo::StreamInfo& stream_info) {
          absl::optional<std::string> result;
          if (!stream_info.upstreamTransportFailureReason().empty()) {
            result = stream_info.upstreamTransportFailureReason();
          }
          return result;
        });
  } else if (field_name == "HOSTNAME") {
    std::string hostname = AccessLogFormatUtils::getHostname();
    field_extractor_ = std::make_unique<StreamInfoOptionalStringFieldExtractor>(
        [hostname](const StreamInfo::StreamInfo&) { return hostname; });
  } else {
    throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", field_name));
  }
}

std::string StreamInfoFormatter::format(const Http::RequestHeaderMap&,
                                        const Http::ResponseHeaderMap&,
                                        const Http::ResponseTrailerMap&,
                                        const StreamInfo::StreamInfo& stream_info) const {
  return field_extractor_->extract(stream_info);
}

ProtobufWkt::Value
StreamInfoFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&,
                                 const StreamInfo::StreamInfo& stream_info) const {
  return field_extractor_->extractValue(stream_info);
}

PlainStringFormatter::PlainStringFormatter(const std::string& str) { str_.set_string_value(str); }

std::string PlainStringFormatter::format(const Http::RequestHeaderMap&,
                                         const Http::ResponseHeaderMap&,
                                         const Http::ResponseTrailerMap&,
                                         const StreamInfo::StreamInfo&) const {
  return str_.string_value();
}

ProtobufWkt::Value PlainStringFormatter::formatValue(const Http::RequestHeaderMap&,
                                                     const Http::ResponseHeaderMap&,
                                                     const Http::ResponseTrailerMap&,
                                                     const StreamInfo::StreamInfo&) const {
  return str_;
}

HeaderFormatter::HeaderFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

const Http::HeaderEntry* HeaderFormatter::findHeader(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = headers.get(main_header_);

  if (!header && !alternative_header_.get().empty()) {
    return headers.get(alternative_header_);
  }

  return header;
}

std::string HeaderFormatter::format(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = findHeader(headers);
  if (!header) {
    return UnspecifiedValueString;
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

std::string ResponseHeaderFormatter::format(const Http::RequestHeaderMap&,
                                            const Http::ResponseHeaderMap& response_headers,
                                            const Http::ResponseTrailerMap&,
                                            const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(response_headers);
}

ProtobufWkt::Value ResponseHeaderFormatter::formatValue(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap& response_headers,
    const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(response_headers);
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string RequestHeaderFormatter::format(const Http::RequestHeaderMap& request_headers,
                                           const Http::ResponseHeaderMap&,
                                           const Http::ResponseTrailerMap&,
                                           const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(request_headers);
}

ProtobufWkt::Value
RequestHeaderFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                    const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
                                    const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(request_headers);
}

ResponseTrailerFormatter::ResponseTrailerFormatter(const std::string& main_header,
                                                   const std::string& alternative_header,
                                                   absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string ResponseTrailerFormatter::format(const Http::RequestHeaderMap&,
                                             const Http::ResponseHeaderMap&,
                                             const Http::ResponseTrailerMap& response_trailers,
                                             const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(response_trailers);
}

ProtobufWkt::Value
ResponseTrailerFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                      const Http::ResponseTrailerMap& response_trailers,
                                      const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::formatValue(response_trailers);
}

MetadataFormatter::MetadataFormatter(const std::string& filter_namespace,
                                     const std::vector<std::string>& path,
                                     absl::optional<size_t> max_length)
    : filter_namespace_(filter_namespace), path_(path), max_length_(max_length) {}

std::string
MetadataFormatter::formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
  ProtobufWkt::Value value = formatMetadataValue(metadata);
  if (value.kind_case() == ProtobufWkt::Value::kNullValue) {
    return UnspecifiedValueString;
  }

  std::string json = MessageUtil::getJsonStringFromMessage(value, false, true);
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

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by @htuch.
// See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length) {}

std::string DynamicMetadataFormatter::format(const Http::RequestHeaderMap&,
                                             const Http::ResponseHeaderMap&,
                                             const Http::ResponseTrailerMap&,
                                             const StreamInfo::StreamInfo& stream_info) const {
  return MetadataFormatter::formatMetadata(stream_info.dynamicMetadata());
}

ProtobufWkt::Value
DynamicMetadataFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                      const Http::ResponseTrailerMap&,
                                      const StreamInfo::StreamInfo& stream_info) const {
  return MetadataFormatter::formatMetadataValue(stream_info.dynamicMetadata());
}

FilterStateFormatter::FilterStateFormatter(const std::string& key,
                                           absl::optional<size_t> max_length)
    : key_(key), max_length_(max_length) {}

ProtobufTypes::MessagePtr
FilterStateFormatter::filterState(const StreamInfo::StreamInfo& stream_info) const {
  const StreamInfo::FilterState& filter_state = stream_info.filterState();
  if (!filter_state.hasDataWithName(key_)) {
    return nullptr;
  }

  const auto& object = filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(key_);
  return object.serializeAsProto();
}

std::string FilterStateFormatter::format(const Http::RequestHeaderMap&,
                                         const Http::ResponseHeaderMap&,
                                         const Http::ResponseTrailerMap&,
                                         const StreamInfo::StreamInfo& stream_info) const {
  ProtobufTypes::MessagePtr proto = filterState(stream_info);
  if (proto == nullptr) {
    return UnspecifiedValueString;
  }

  std::string value;
  const auto status = Protobuf::util::MessageToJsonString(*proto, &value);
  if (!status.ok()) {
    // If the message contains an unknown Any (from WASM or Lua), MessageToJsonString will fail.
    // TODO(lizan): add support of unknown Any.
    return UnspecifiedValueString;
  }

  truncate(value, max_length_);
  return value;
}

ProtobufWkt::Value
FilterStateFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&,
                                  const StreamInfo::StreamInfo& stream_info) const {
  ProtobufTypes::MessagePtr proto = filterState(stream_info);
  if (proto == nullptr) {
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

StartTimeFormatter::StartTimeFormatter(const std::string& format) : date_formatter_(format) {}

std::string StartTimeFormatter::format(const Http::RequestHeaderMap&,
                                       const Http::ResponseHeaderMap&,
                                       const Http::ResponseTrailerMap&,
                                       const StreamInfo::StreamInfo& stream_info) const {
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(stream_info.startTime());
  } else {
    return date_formatter_.fromTime(stream_info.startTime());
  }
}

ProtobufWkt::Value
StartTimeFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                const Http::ResponseHeaderMap& response_headers,
                                const Http::ResponseTrailerMap& response_trailers,
                                const StreamInfo::StreamInfo& stream_info) const {
  return ValueUtil::stringValue(
      format(request_headers, response_headers, response_trailers, stream_info));
}

} // namespace AccessLog
} // namespace Envoy
