#include "common/access_log/access_log_formatter.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/http/utility.h"
#include "common/stream_info/utility.h"

#include "absl/strings/str_split.h"
#include "fmt/format.h"

using Envoy::Config::Metadata;

namespace Envoy {
namespace AccessLog {

static const std::string UnspecifiedValueString = "-";

const std::string AccessLogFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

FormatterPtr AccessLogFormatUtils::defaultAccessLogFormatter() {
  return FormatterPtr{new FormatterImpl(DEFAULT_FORMAT)};
}

std::string
AccessLogFormatUtils::durationToString(const absl::optional<std::chrono::nanoseconds>& time) {
  if (time) {
    return durationToString(time.value());
  } else {
    return UnspecifiedValueString;
  }
}

std::string AccessLogFormatUtils::durationToString(const std::chrono::nanoseconds& time) {
  return fmt::format_int(std::chrono::duration_cast<std::chrono::milliseconds>(time).count()).str();
}

const std::string&
AccessLogFormatUtils::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return UnspecifiedValueString;
}

FormatterImpl::FormatterImpl(const std::string& format) {
  providers_ = AccessLogFormatParser::parse(format);
}

std::string FormatterImpl::format(const Http::HeaderMap& request_headers,
                                  const Http::HeaderMap& response_headers,
                                  const Http::HeaderMap& response_trailers,
                                  const StreamInfo::StreamInfo& stream_info) const {
  std::string log_line;
  log_line.reserve(256);

  for (const FormatterProviderPtr& provider : providers_) {
    log_line += provider->format(request_headers, response_headers, response_trailers, stream_info);
  }

  return log_line;
}

JsonFormatterImpl::JsonFormatterImpl(std::unordered_map<std::string, std::string>& format_mapping) {
  for (const auto& pair : format_mapping) {
    auto providers = AccessLogFormatParser::parse(pair.second);
    json_output_format_.emplace(pair.first, FormatterPtr{new FormatterImpl(pair.second)});
  }
}

std::string JsonFormatterImpl::format(const Http::HeaderMap& request_headers,
                                      const Http::HeaderMap& response_headers,
                                      const Http::HeaderMap& response_trailers,
                                      const StreamInfo::StreamInfo& stream_info) const {
  const auto output_map = toMap(request_headers, response_headers, response_trailers, stream_info);

  ProtobufWkt::Struct output_struct;
  for (const auto& pair : output_map) {
    ProtobufWkt::Value string_value;
    string_value.set_string_value(pair.second);
    (*output_struct.mutable_fields())[pair.first] = string_value;
  }

  std::string log_line;
  const auto conversion_status = ProtobufUtil::MessageToJsonString(output_struct, &log_line);
  if (!conversion_status.ok()) {
    log_line =
        fmt::format("Error serializing access log to JSON: {}", conversion_status.ToString());
  }

  return log_line;
}

std::unordered_map<std::string, std::string> JsonFormatterImpl::toMap(
    const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
    const Http::HeaderMap& response_trailers, const StreamInfo::StreamInfo& stream_info) const {
  std::unordered_map<std::string, std::string> output;
  for (const auto& pair : json_output_format_) {
    output.emplace(pair.first, pair.second->format(request_headers, response_headers,
                                                   response_trailers, stream_info));
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
        fmt::format("More than 1 alternative header specified in token: {}", token));
  }
  if (subs.size() == 1) {
    alternative_header = subs.front();
  } else {
    alternative_header = "";
  }
}

void AccessLogFormatParser::parseCommand(const std::string& token, const size_t start,
                                         const std::string& separator, std::string& main,
                                         std::vector<std::string>& sub_items,
                                         absl::optional<size_t>& max_length) {
  size_t end_request = token.find(')', start);
  sub_items.clear();
  if (end_request != token.length() - 1) {
    // Closing bracket is not found.
    if (end_request == std::string::npos) {
      throw EnvoyException(fmt::format("Closing bracket is missing in token: {}", token));
    }

    // Closing bracket should be either last one or followed by ':' to denote limitation.
    if (token[end_request + 1] != ':') {
      throw EnvoyException(fmt::format("Incorrect position of ')' in token: {}", token));
    }

    std::string length_str = token.substr(end_request + 2);
    uint64_t length_value;

    if (!StringUtil::atoul(length_str.c_str(), length_value)) {
      throw EnvoyException(fmt::format("Length must be an integer, given: {}", length_str));
    }

    max_length = length_value;
  }

  const std::string name_data = token.substr(start, end_request - start);
  const std::vector<std::string> keys = absl::StrSplit(name_data, separator);
  if (!keys.empty()) {
    // The main value is the first key
    main = keys.at(0);
    if (keys.size() > 1) {
      // Sub items contain additional keys
      sub_items.insert(sub_items.end(), keys.begin() + 1, keys.end());
    }
  }
}

// TODO(derekargueta): #2967 - Rewrite AccessLogformatter with parser library & formal grammar
std::vector<FormatterProviderPtr> AccessLogFormatParser::parse(const std::string& format) {
  std::string current_token;
  std::vector<FormatterProviderPtr> formatters;
  const std::string DYNAMIC_META_TOKEN = "DYNAMIC_METADATA(";
  const std::regex command_w_args_regex(R"EOF(%([A-Z]|_)+(\([^\)]*\))?(:[0-9]+)?(%))EOF");

  for (size_t pos = 0; pos < format.length(); ++pos) {
    if (format[pos] == '%') {
      if (!current_token.empty()) {
        formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
        current_token = "";
      }

      std::smatch m;
      std::string search_space = format.substr(pos);
      if (!(std::regex_search(search_space, m, command_w_args_regex) || m.position() == 0)) {
        throw EnvoyException(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      const std::string match = m.str(0);
      const std::string token = match.substr(1, match.length() - 2);
      pos += 1;
      int command_end_position = pos + token.length();

      if (token.find("REQ(") == 0) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, ReqParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new RequestHeaderFormatter(main_header, alternative_header, max_length)});
      } else if (token.find("RESP(") == 0) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, RespParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new ResponseHeaderFormatter(main_header, alternative_header, max_length)});
      } else if (token.find("TRAILER(") == 0) {
        std::string main_header, alternative_header;
        absl::optional<size_t> max_length;

        parseCommandHeader(token, TrailParamStart, main_header, alternative_header, max_length);

        formatters.emplace_back(FormatterProviderPtr{
            new ResponseTrailerFormatter(main_header, alternative_header, max_length)});
      } else if (token.find(DYNAMIC_META_TOKEN) == 0) {
        std::string filter_namespace;
        absl::optional<size_t> max_length;
        std::vector<std::string> path;
        const size_t start = DYNAMIC_META_TOKEN.size();

        parseCommand(token, start, ":", filter_namespace, path, max_length);
        formatters.emplace_back(
            FormatterProviderPtr{new DynamicMetadataFormatter(filter_namespace, path, max_length)});
      } else if (token.find("START_TIME") == 0) {
        const size_t parameters_length = pos + StartTimeParamStart + 1;
        const size_t parameters_end = command_end_position - parameters_length;

        const std::string args = token[StartTimeParamStart - 1] == '('
                                     ? token.substr(StartTimeParamStart, parameters_end)
                                     : "";
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

StreamInfoFormatter::StreamInfoFormatter(const std::string& field_name) {

  if (field_name == "REQUEST_DURATION") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return AccessLogFormatUtils::durationToString(stream_info.lastDownstreamRxByteReceived());
    };
  } else if (field_name == "RESPONSE_DURATION") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return AccessLogFormatUtils::durationToString(stream_info.firstUpstreamRxByteReceived());
    };
  } else if (field_name == "RESPONSE_TX_DURATION") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      auto downstream = stream_info.lastDownstreamTxByteSent();
      auto upstream = stream_info.firstUpstreamRxByteReceived();

      if (downstream && upstream) {
        auto val = downstream.value() - upstream.value();
        return AccessLogFormatUtils::durationToString(val);
      }

      return UnspecifiedValueString;
    };
  } else if (field_name == "BYTES_RECEIVED") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return fmt::format_int(stream_info.bytesReceived()).str();
    };
  } else if (field_name == "PROTOCOL") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return AccessLogFormatUtils::protocolToString(stream_info.protocol());
    };
  } else if (field_name == "RESPONSE_CODE") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.responseCode() ? fmt::format_int(stream_info.responseCode().value()).str()
                                        : "0";
    };
  } else if (field_name == "BYTES_SENT") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return fmt::format_int(stream_info.bytesSent()).str();
    };
  } else if (field_name == "DURATION") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return AccessLogFormatUtils::durationToString(stream_info.requestComplete());
    };
  } else if (field_name == "RESPONSE_FLAGS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
    };
  } else if (field_name == "UPSTREAM_HOST") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      if (stream_info.upstreamHost()) {
        return stream_info.upstreamHost()->address()->asString();
      } else {
        return UnspecifiedValueString;
      }
    };
  } else if (field_name == "UPSTREAM_CLUSTER") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      std::string upstream_cluster_name;
      if (nullptr != stream_info.upstreamHost()) {
        upstream_cluster_name = stream_info.upstreamHost()->cluster().name();
      }

      return upstream_cluster_name.empty() ? UnspecifiedValueString : upstream_cluster_name;
    };
  } else if (field_name == "UPSTREAM_LOCAL_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.upstreamLocalAddress() != nullptr
                 ? stream_info.upstreamLocalAddress()->asString()
                 : UnspecifiedValueString;
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamLocalAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const Envoy::StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamLocalAddress());
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return stream_info.downstreamRemoteAddress()->asString();
    };
  } else if (field_name == "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      return StreamInfo::Utility::formatDownstreamAddressNoPort(
          *stream_info.downstreamRemoteAddress());
    };
  } else if (field_name == "REQUESTED_SERVER_NAME") {
    field_extractor_ = [](const StreamInfo::StreamInfo& stream_info) {
      if (!stream_info.requestedServerName().empty()) {
        return stream_info.requestedServerName();
      } else {
        return UnspecifiedValueString;
      }
    };
  } else {
    throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", field_name));
  }
}

std::string StreamInfoFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                        const Http::HeaderMap&,
                                        const StreamInfo::StreamInfo& stream_info) const {
  return field_extractor_(stream_info);
}

PlainStringFormatter::PlainStringFormatter(const std::string& str) : str_(str) {}

std::string PlainStringFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                         const Http::HeaderMap&,
                                         const StreamInfo::StreamInfo&) const {
  return str_;
}

HeaderFormatter::HeaderFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 absl::optional<size_t> max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

std::string HeaderFormatter::format(const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = headers.get(main_header_);

  if (!header && !alternative_header_.get().empty()) {
    header = headers.get(alternative_header_);
  }

  std::string header_value_string;
  if (!header) {
    header_value_string = UnspecifiedValueString;
  } else {
    header_value_string = header->value().c_str();
  }

  if (max_length_ && header_value_string.length() > max_length_.value()) {
    return header_value_string.substr(0, max_length_.value());
  }

  return header_value_string;
}

ResponseHeaderFormatter::ResponseHeaderFormatter(const std::string& main_header,
                                                 const std::string& alternative_header,
                                                 absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string ResponseHeaderFormatter::format(const Http::HeaderMap&,
                                            const Http::HeaderMap& response_headers,
                                            const Http::HeaderMap&,
                                            const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(response_headers);
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string RequestHeaderFormatter::format(const Http::HeaderMap& request_headers,
                                           const Http::HeaderMap&, const Http::HeaderMap&,
                                           const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(request_headers);
}

ResponseTrailerFormatter::ResponseTrailerFormatter(const std::string& main_header,
                                                   const std::string& alternative_header,
                                                   absl::optional<size_t> max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string ResponseTrailerFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                             const Http::HeaderMap& response_trailers,
                                             const StreamInfo::StreamInfo&) const {
  return HeaderFormatter::format(response_trailers);
}

MetadataFormatter::MetadataFormatter(const std::string& filter_namespace,
                                     const std::vector<std::string>& path,
                                     absl::optional<size_t> max_length)
    : filter_namespace_(filter_namespace), path_(path), max_length_(max_length) {}

std::string MetadataFormatter::format(const envoy::api::v2::core::Metadata& metadata) const {
  const Protobuf::Message* data;
  if (path_.empty()) {
    const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
    if (filter_it == metadata.filter_metadata().end()) {
      return UnspecifiedValueString;
    }
    data = &(filter_it->second);
  } else {
    const ProtobufWkt::Value& val = Metadata::metadataValue(metadata, filter_namespace_, path_);
    if (val.kind_case() == ProtobufWkt::Value::KindCase::KIND_NOT_SET) {
      return UnspecifiedValueString;
    }
    data = &val;
  }
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(*data, &json);
  RELEASE_ASSERT(status.ok(), "");
  if (max_length_ && json.length() > max_length_.value()) {
    return json.substr(0, max_length_.value());
  }
  return json;
}

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by @htuch.
// See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(const std::string& filter_namespace,
                                                   const std::vector<std::string>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length) {}

std::string DynamicMetadataFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                             const Http::HeaderMap&,
                                             const StreamInfo::StreamInfo& stream_info) const {
  return MetadataFormatter::format(stream_info.dynamicMetadata());
}

StartTimeFormatter::StartTimeFormatter(const std::string& format) : date_formatter_(format) {}

std::string StartTimeFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                       const Http::HeaderMap&,
                                       const StreamInfo::StreamInfo& stream_info) const {
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(stream_info.startTime());
  } else {
    return date_formatter_.fromTime(stream_info.startTime());
  }
}

} // namespace AccessLog
} // namespace Envoy
