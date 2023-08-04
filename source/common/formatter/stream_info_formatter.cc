#include "source/common/formatter/stream_info_formatter.h"

#include <regex>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

const std::regex& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "%[-_0^#]*[1-9]*(E|O)?n");
}

void CommandSyntaxChecker::verifySyntax(CommandSyntaxFlags flags, const std::string& command,
                                        const std::string& subcommand,
                                        absl::optional<size_t> length) {
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

absl::string_view CommonFormatterUtil::defaultUnspecifiedValueString() {
  return DefaultUnspecifiedValueString;
}

const ProtobufWkt::Value& CommonFormatterUtil::unspecifiedProtobufValue() {
  return ValueUtil::nullValue();
}

void CommonFormatterUtil::truncate(std::string& str, absl::optional<uint32_t> max_length) {
  if (!max_length) {
    return;
  }

  if (str.length() > max_length.value()) {
    str.resize(max_length.value());
  }
}

const absl::optional<std::reference_wrapper<const std::string>>
CommonFormatterUtil::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return absl::nullopt;
}

const std::string&
CommonFormatterUtil::protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return DefaultUnspecifiedValueString;
}

const absl::optional<std::string> CommonFormatterUtil::getHostname() {
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

const std::string CommonFormatterUtil::getHostnameOrDefault() {
  absl::optional<std::string> hostname = getHostname();
  if (hostname.has_value()) {
    return hostname.value();
  }
  return DefaultUnspecifiedValueString;
}

void CommonFormatterUtil::parseSubcommandHeaders(const std::string& subcommand,
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

namespace StreamInfoOnly {

// StreamInfo std::string field extractor.
class StreamInfoStringFieldExtractor : public FieldExtractor {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // FieldExtractor
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
class StreamInfoDurationFieldExtractor : public FieldExtractor {
public:
  using FieldExtractor =
      std::function<absl::optional<std::chrono::nanoseconds>(const StreamInfo::StreamInfo&)>;

  StreamInfoDurationFieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // FieldExtractor
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
      return CommonFormatterUtil::unspecifiedProtobufValue();
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
class StreamInfoUInt64FieldExtractor : public FieldExtractor {
public:
  using FieldExtractor = std::function<uint64_t(const StreamInfo::StreamInfo&)>;

  StreamInfoUInt64FieldExtractor(FieldExtractor f) : field_extractor_(f) {}

  // FieldExtractor
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
class StreamInfoAddressFieldExtractor : public FieldExtractor {
public:
  using FieldExtractor =
      std::function<Network::Address::InstanceConstSharedPtr(const StreamInfo::StreamInfo&)>;

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoAddressFieldExtractionType::WithPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> withoutPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoAddressFieldExtractionType::WithoutPort);
  }

  static std::unique_ptr<StreamInfoAddressFieldExtractor> justPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFieldExtractor>(
        f, StreamInfoAddressFieldExtractionType::JustPort);
  }

  StreamInfoAddressFieldExtractor(FieldExtractor f,
                                  StreamInfoAddressFieldExtractionType extraction_type)
      : field_extractor_(f), extraction_type_(extraction_type) {}

  // FieldExtractor
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
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.format_ports_as_numbers")) {
      if (extraction_type_ == StreamInfoAddressFieldExtractionType::JustPort) {
        const auto port = StreamInfo::Utility::extractDownstreamAddressJustPort(*address);
        if (port) {
          return ValueUtil::numberValue(*port);
        }
        return CommonFormatterUtil::unspecifiedProtobufValue();
      }
    }
    return ValueUtil::stringValue(toString(*address));
  }

private:
  std::string toString(const Network::Address::Instance& address) const {
    switch (extraction_type_) {
    case StreamInfoAddressFieldExtractionType::WithoutPort:
      return StreamInfo::Utility::formatDownstreamAddressNoPort(address);
    case StreamInfoAddressFieldExtractionType::JustPort:
      return StreamInfo::Utility::formatDownstreamAddressJustPort(address);
    case StreamInfoAddressFieldExtractionType::WithPort:
    default:
      return address.asString();
    }
  }

  FieldExtractor field_extractor_;
  const StreamInfoAddressFieldExtractionType extraction_type_;
};

// Ssl::ConnectionInfo std::string field extractor.
class StreamInfoSslConnectionInfoFieldExtractor : public FieldExtractor {
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
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class StreamInfoUpstreamSslConnectionInfoFieldExtractor : public FieldExtractor {
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
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class MetadataFieldExtractor : public FieldExtractor {
public:
  using GetMetadataFunction =
      std::function<const envoy::config::core::v3::Metadata*(const StreamInfo::StreamInfo&)>;
  MetadataFieldExtractor(const std::string& filter_namespace, const std::vector<std::string>& path,
                         absl::optional<size_t> max_length, GetMetadataFunction get_func)
      : filter_namespace_(filter_namespace), path_(path), max_length_(max_length),
        get_func_(get_func) {}

  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {

    auto metadata = get_func_(stream_info);
    return (metadata != nullptr) ? formatMetadata(*metadata) : absl::nullopt;
  }

  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    auto metadata = get_func_(stream_info);
    return formatMetadataValue((metadata != nullptr) ? *metadata
                                                     : envoy::config::core::v3::Metadata());
  }

protected:
  absl::optional<std::string>
  formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
    ProtobufWkt::Value value = formatMetadataValue(metadata);
    if (value.kind_case() == ProtobufWkt::Value::kNullValue) {
      return absl::nullopt;
    }

    std::string str;
    if (value.kind_case() == ProtobufWkt::Value::kStringValue) {
      str = value.string_value();
    } else {
#ifdef ENVOY_ENABLE_YAML
      absl::StatusOr<std::string> json_or_error =
          MessageUtil::getJsonStringFromMessage(value, false, true);
      if (json_or_error.ok()) {
        str = json_or_error.value();
      } else {
        str = json_or_error.status().message();
      }
#else
      IS_ENVOY_BUG("Json support compiled out");
#endif
    }
    CommonFormatterUtil::truncate(str, max_length_);
    return str;
  }

  ProtobufWkt::Value formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const {
    if (path_.empty()) {
      const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
      if (filter_it == metadata.filter_metadata().end()) {
        return CommonFormatterUtil::unspecifiedProtobufValue();
      }
      ProtobufWkt::Value output;
      output.mutable_struct_value()->CopyFrom(filter_it->second);
      return output;
    }

    const ProtobufWkt::Value& val =
        Config::Metadata::metadataValue(&metadata, filter_namespace_, path_);
    if (val.kind_case() == ProtobufWkt::Value::KindCase::KIND_NOT_SET) {
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    return val;
  }

private:
  std::string filter_namespace_;
  std::vector<std::string> path_;
  absl::optional<size_t> max_length_;
  GetMetadataFunction get_func_;
};

/**
 * FieldExtractor for DynamicMetadata from StreamInfo.
 */
class DynamicMetadataFieldExtractor : public MetadataFieldExtractor {
public:
  DynamicMetadataFieldExtractor(const std::string& filter_namespace,
                                const std::vector<std::string>& path,
                                absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
                               [](const StreamInfo::StreamInfo& stream_info) {
                                 return &stream_info.dynamicMetadata();
                               }) {}
};

/**
 * FieldExtractor for ClusterMetadata from StreamInfo.
 */
class ClusterMetadataFieldExtractor : public MetadataFieldExtractor {
public:
  ClusterMetadataFieldExtractor(const std::string& filter_namespace,
                                const std::vector<std::string>& path,
                                absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
                               [](const StreamInfo::StreamInfo& stream_info)
                                   -> const envoy::config::core::v3::Metadata* {
                                 auto cluster_info = stream_info.upstreamClusterInfo();
                                 if (!cluster_info.has_value() || cluster_info.value() == nullptr) {
                                   return nullptr;
                                 }
                                 return &cluster_info.value()->metadata();
                               }) {}
};

/**
 * FieldExtractor for UpstreamHostMetadata from StreamInfo.
 */
class UpstreamHostMetadataFieldExtractor : public MetadataFieldExtractor {
public:
  UpstreamHostMetadataFieldExtractor(const std::string& filter_namespace,
                                     const std::vector<std::string>& path,
                                     absl::optional<size_t> max_length)
      : MetadataFieldExtractor(filter_namespace, path, max_length,
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
};

/**
 * FieldExtractor for FilterState from StreamInfo.
 */
class FilterStateFieldExtractor : public FieldExtractor {
public:
  static std::unique_ptr<FilterStateFieldExtractor>
  create(const std::string& format, absl::optional<size_t> max_length, bool is_upstream) {

    std::string key, serialize_type;
    static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
    static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};

    CommonFormatterUtil::parseSubcommand(format, ':', key, serialize_type);
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

    return std::make_unique<FilterStateFieldExtractor>(key, max_length, serialize_as_string,
                                                       is_upstream);
  }

  FilterStateFieldExtractor(const std::string& key, absl::optional<size_t> max_length,
                            bool serialize_as_string, bool is_upstream)
      : key_(key), max_length_(max_length), serialize_as_string_(serialize_as_string),
        is_upstream_(is_upstream) {}

  // FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
    if (!state) {
      return absl::nullopt;
    }

    if (serialize_as_string_) {
      absl::optional<std::string> plain_value = state->serializeAsString();
      if (plain_value.has_value()) {
        CommonFormatterUtil::truncate(plain_value.value(), max_length_);
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

    CommonFormatterUtil::truncate(value, max_length_);
    return value;
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
    if (!state) {
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    if (serialize_as_string_) {
      absl::optional<std::string> plain_value = state->serializeAsString();
      if (plain_value.has_value()) {
        CommonFormatterUtil::truncate(plain_value.value(), max_length_);
        return ValueUtil::stringValue(plain_value.value());
      }
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (!proto) {
      return CommonFormatterUtil::unspecifiedProtobufValue();
    }

#ifdef ENVOY_ENABLE_YAML
    ProtobufWkt::Value val;
    if (MessageUtil::jsonConvertValue(*proto, val)) {
      return val;
    }
#endif
    return CommonFormatterUtil::unspecifiedProtobufValue();
  }

private:
  const Envoy::StreamInfo::FilterState::Object*
  filterState(const StreamInfo::StreamInfo& stream_info) const {
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

  std::string key_;
  absl::optional<size_t> max_length_;

  bool serialize_as_string_;
  const bool is_upstream_;
};

/**
 * Base FieldExtractor for system times from StreamInfo.
 */
class SystemTimeFieldExtractor : public FieldExtractor {
public:
  using TimeFieldExtractor =
      std::function<absl::optional<SystemTime>(const StreamInfo::StreamInfo& stream_info)>;
  using TimeFieldExtractorPtr = std::unique_ptr<TimeFieldExtractor>;

  SystemTimeFieldExtractor(const std::string& format, TimeFieldExtractorPtr f)
      : date_formatter_(format), time_field_extractor_(std::move(f)) {
    // Validate the input specifier here. The formatted string may be destined for a header, and
    // should not contain invalid characters {NUL, LR, CF}.
    if (std::regex_search(format, getSystemTimeFormatNewlinePattern())) {
      throw EnvoyException("Invalid header configuration. Format string contains newline.");
    }
  }

  // FieldExtractor
  absl::optional<std::string> extract(const StreamInfo::StreamInfo& stream_info) const override {
    const auto time_field = (*time_field_extractor_)(stream_info);
    if (!time_field.has_value()) {
      return absl::nullopt;
    }
    if (date_formatter_.formatString().empty()) {
      return AccessLogDateTimeFormatter::fromTime(time_field.value());
    }
    return date_formatter_.fromTime(time_field.value());
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(extract(stream_info));
  }

private:
  const Envoy::DateFormatter date_formatter_;
  const TimeFieldExtractorPtr time_field_extractor_;
};

/**
 * SystemTimeFieldExtractor (FieldExtractor) for request start time from StreamInfo.
 */
class StartTimeFieldExtractor : public SystemTimeFieldExtractor {
public:
  // A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
  // an access log command that starts with `START_TIME`.
  StartTimeFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.startTime();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FieldExtractor) for downstream cert start time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVStartFieldExtractor : public SystemTimeFieldExtractor {
public:
  DownstreamPeerCertVStartFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  const auto connection_info =
                      stream_info.downstreamAddressProvider().sslConnection();
                  return connection_info != nullptr ? connection_info->validFromPeerCertificate()
                                                    : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FieldExtractor) for downstream cert end time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVEndFieldExtractor : public SystemTimeFieldExtractor {
public:
  DownstreamPeerCertVEndFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  const auto connection_info =
                      stream_info.downstreamAddressProvider().sslConnection();
                  return connection_info != nullptr ? connection_info->expirationPeerCertificate()
                                                    : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor (FieldExtractor) for upstream cert start time from the StreamInfo's
 * upstreamInfo.
 */
class UpstreamPeerCertVStartFieldExtractor : public SystemTimeFieldExtractor {
public:
  UpstreamPeerCertVStartFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.upstreamInfo() &&
                                 stream_info.upstreamInfo()->upstreamSslConnection() != nullptr
                             ? stream_info.upstreamInfo()
                                   ->upstreamSslConnection()
                                   ->validFromPeerCertificate()
                             : absl::optional<SystemTime>();
                })) {}
};

/**
 * SystemTimeFieldExtractor for upstream cert end time from the StreamInfo's
 * upstreamInfo.
 */
class UpstreamPeerCertVEndFieldExtractor : public SystemTimeFieldExtractor {
public:
  UpstreamPeerCertVEndFieldExtractor(const std::string& format)
      : SystemTimeFieldExtractor(
            format,
            std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                  return stream_info.upstreamInfo() &&
                                 stream_info.upstreamInfo()->upstreamSslConnection() != nullptr
                             ? stream_info.upstreamInfo()
                                   ->upstreamSslConnection()
                                   ->expirationPeerCertificate()
                             : absl::optional<SystemTime>();
                })) {}
};

/**
 * FieldExtractor for environment. If no valid environment value then
 */
class EnvironmentFieldExtractor : public FieldExtractor {
public:
  EnvironmentFieldExtractor(const std::string& key, absl::optional<size_t> max_length) {
    ASSERT(!key.empty());

    const char* env_value = std::getenv(key.c_str());
    if (env_value != nullptr) {
      std::string env_string = env_value;
      CommonFormatterUtil::truncate(env_string, max_length);
      str_.set_string_value(env_string);
      return;
    }
    str_.set_string_value(DefaultUnspecifiedValueString);
  }

  // FormatterProvider
  absl::optional<std::string> extract(const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo&) const override { return str_; }

private:
  ProtobufWkt::Value str_;
};

const FieldExtractorLookupTbl& getKnownFieldExtractors() {
  CONSTRUCT_ON_FIRST_USE(FieldExtractorLookupTbl,
                         {
                             {"REQUEST_DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       StreamInfo::TimingUtility timing(stream_info);
                                       return timing.lastDownstreamRxByteReceived();
                                     });
                               }}},
                             {"REQUEST_TX_DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       StreamInfo::TimingUtility timing(stream_info);
                                       return timing.lastUpstreamTxByteSent();
                                     });
                               }}},
                             {"RESPONSE_DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       StreamInfo::TimingUtility timing(stream_info);
                                       return timing.firstUpstreamRxByteReceived();
                                     });
                               }}},
                             {"RESPONSE_TX_DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       StreamInfo::TimingUtility timing(stream_info);
                                       return timing.downstreamHandshakeComplete();
                                     });
                               }}},
                             {"ROUNDTRIP_DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       StreamInfo::TimingUtility timing(stream_info);
                                       return timing.lastDownstreamAckReceived();
                                     });
                               }}},
                             {"BYTES_RECEIVED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.bytesReceived();
                                     });
                               }}},
                             {"BYTES_RETRANSMITTED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.bytesRetransmitted();
                                     });
                               }}},
                             {"PACKETS_RETRANSMITTED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.packetsRetransmitted();
                                     });
                               }}},
                             {"UPSTREAM_WIRE_BYTES_RECEIVED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getUpstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                                     });
                               }}},
                             {"UPSTREAM_HEADER_BYTES_RECEIVED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getUpstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                                     });
                               }}},
                             {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getDownstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                                     });
                               }}},
                             {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getDownstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                                     });
                               }}},
                             {"PROTOCOL",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return CommonFormatterUtil::protocolToString(
                                           stream_info.protocol());
                                     });
                               }}},
                             {"UPSTREAM_PROTOCOL",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.upstreamInfo()
                                                  ? CommonFormatterUtil::protocolToString(
                                                        stream_info.upstreamInfo()
                                                            ->upstreamProtocol())
                                                  : absl::nullopt;
                                     });
                               }}},
                             {"RESPONSE_CODE",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.responseCode().value_or(0);
                                     });
                               }}},
                             {"RESPONSE_CODE_DETAILS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.responseCodeDetails();
                                     });
                               }}},
                             {"CONNECTION_TERMINATION_DETAILS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.connectionTerminationDetails();
                                     });
                               }}},
                             {"BYTES_SENT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.bytesSent();
                                     });
                               }}},
                             {"UPSTREAM_WIRE_BYTES_SENT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getUpstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                                     });
                               }}},
                             {"UPSTREAM_HEADER_BYTES_SENT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getUpstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                                     });
                               }}},
                             {"DOWNSTREAM_WIRE_BYTES_SENT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getDownstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                                     });
                               }}},
                             {"DOWNSTREAM_HEADER_BYTES_SENT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       const auto& bytes_meter =
                                           stream_info.getDownstreamBytesMeter();
                                       return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                                     });
                               }}},
                             {"DURATION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoDurationFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.currentDuration();
                                     });
                               }}},
                             {"RESPONSE_FLAGS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return StreamInfo::ResponseFlagUtils::toShortString(
                                           stream_info);
                                     });
                               }}},
                             {"RESPONSE_FLAGS_LONG",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return StreamInfo::ResponseFlagUtils::toString(stream_info);
                                     });
                               }}},
                             {"UPSTREAM_HOST",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
                                       if (stream_info.upstreamInfo() &&
                                           stream_info.upstreamInfo()->upstreamHost()) {
                                         return stream_info.upstreamInfo()
                                             ->upstreamHost()
                                             ->address();
                                       }
                                       return nullptr;
                                     });
                               }}},
                             {"UPSTREAM_CLUSTER",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withoutPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::justPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
                                       if (stream_info.upstreamInfo() &&
                                           stream_info.upstreamInfo()->upstreamHost()) {
                                         return stream_info.upstreamInfo()
                                             ->upstreamHost()
                                             ->address();
                                       }
                                       return nullptr;
                                     });
                               }}},
                             {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withoutPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
                                       if (stream_info.upstreamInfo() &&
                                           stream_info.upstreamInfo()->upstreamHost()) {
                                         return stream_info.upstreamInfo()
                                             ->upstreamHost()
                                             ->address();
                                       }
                                       return nullptr;
                                     });
                               }}},
                             {"UPSTREAM_REMOTE_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::justPort(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> std::shared_ptr<
                                             const Envoy::Network::Address::Instance> {
                                       if (stream_info.upstreamInfo() &&
                                           stream_info.upstreamInfo()->upstreamHost()) {
                                         return stream_info.upstreamInfo()
                                             ->upstreamHost()
                                             ->address();
                                       }
                                       return nullptr;
                                     });
                               }}},
                             {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.attemptCount().value_or(0);
                                     });
                               }}},
                             {"UPSTREAM_TLS_CIPHER",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.ciphersuiteString();
                                     });
                               }}},
                             {"UPSTREAM_TLS_VERSION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.tlsVersion();
                                     });
                               }}},
                             {"UPSTREAM_TLS_SESSION_ID",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.sessionId();
                                     });
                               }}},
                             {"UPSTREAM_PEER_ISSUER",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.issuerPeerCertificate();
                                     });
                               }}},
                             {"UPSTREAM_PEER_CERT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.urlEncodedPemEncodedPeerCertificate();
                                     });
                               }}},
                             {"UPSTREAM_PEER_SUBJECT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<
                                     StreamInfoUpstreamSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.subjectPeerCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_ADDRESS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .localAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withoutPort(
                                     [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .localAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::justPort(
                                     [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .localAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_REMOTE_ADDRESS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .remoteAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withoutPort(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .remoteAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_REMOTE_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::justPort(
                                     [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .remoteAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withPort(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .directRemoteAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::withoutPort(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .directRemoteAddress();
                                     });
                               }}},
                             {"DOWNSTREAM_DIRECT_REMOTE_PORT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return StreamInfoAddressFieldExtractor::justPort(
                                     [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .directRemoteAddress();
                                     });
                               }}},
                             {"CONNECTION_ID",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoUInt64FieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       return stream_info.downstreamAddressProvider()
                                           .connectionID()
                                           .value_or(0);
                                     });
                               }}},
                             {"REQUESTED_SERVER_NAME",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       absl::optional<std::string> result;
                                       if (!stream_info.downstreamAddressProvider()
                                                .requestedServerName()
                                                .empty()) {
                                         result =
                                             std::string(stream_info.downstreamAddressProvider()
                                                             .requestedServerName());
                                       }
                                       return result;
                                     });
                               }}},
                             {"ROUTE_NAME",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(connection_info.uriSanPeerCertificate(),
                                                            ",");
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_DNS_SAN",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(
                                           connection_info.dnsSansPeerCertificate(), ",");
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_IP_SAN",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(connection_info.ipSansPeerCertificate(),
                                                            ",");
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_URI_SAN",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(
                                           connection_info.uriSanLocalCertificate(), ",");
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_DNS_SAN",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(
                                           connection_info.dnsSansLocalCertificate(), ",");
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_IP_SAN",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return absl::StrJoin(
                                           connection_info.ipSansLocalCertificate(), ",");
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_SUBJECT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.subjectPeerCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_LOCAL_SUBJECT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.subjectLocalCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_TLS_SESSION_ID",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.sessionId();
                                     });
                               }}},
                             {"DOWNSTREAM_TLS_CIPHER",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.ciphersuiteString();
                                     });
                               }}},
                             {"DOWNSTREAM_TLS_VERSION",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.tlsVersion();
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_FINGERPRINT_256",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.sha256PeerCertificateDigest();
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_FINGERPRINT_1",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.sha1PeerCertificateDigest();
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_SERIAL",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.serialNumberPeerCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_ISSUER",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.issuerPeerCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_PEER_CERT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoSslConnectionInfoFieldExtractor>(
                                     [](const Ssl::ConnectionInfo& connection_info) {
                                       return connection_info.urlEncodedPemEncodedPeerCertificate();
                                     });
                               }}},
                             {"DOWNSTREAM_TRANSPORT_FAILURE_REASON",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info) {
                                       absl::optional<std::string> result;
                                       if (!stream_info.downstreamTransportFailureReason()
                                                .empty()) {
                                         result = absl::StrReplaceAll(
                                             stream_info.downstreamTransportFailureReason(),
                                             {{" ", "_"}});
                                       }
                                       return result;
                                     });
                               }}},
                             {"UPSTREAM_TRANSPORT_FAILURE_REASON",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 absl::optional<std::string> hostname =
                                     CommonFormatterUtil::getHostname();
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [hostname](const StreamInfo::StreamInfo&) {
                                       return hostname;
                                     });
                               }}},
                             {"FILTER_CHAIN_NAME",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
                                 return std::make_unique<StreamInfoStringFieldExtractor>(
                                     [](const StreamInfo::StreamInfo& stream_info)
                                         -> absl::optional<std::string> {
                                       return stream_info.virtualClusterName();
                                     });
                               }}},
                             {"TLS_JA3_FINGERPRINT",
                              {CommandSyntaxChecker::COMMAND_ONLY,
                               [](const std::string&, absl::optional<size_t>) {
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
                               [](const std::string&, absl::optional<size_t>) {
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
                               }}},
                             {"START_TIME",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL,
                               [](const std::string& format, absl::optional<size_t>) {
                                 return std::make_unique<SystemTimeFieldExtractor>(
                                     format,
                                     std::make_unique<SystemTimeFieldExtractor::TimeFieldExtractor>(
                                         [](const StreamInfo::StreamInfo& stream_info)
                                             -> absl::optional<SystemTime> {
                                           return stream_info.startTime();
                                         }));
                               }}},
                             {"DYNAMIC_METADATA",
                              {CommandSyntaxChecker::PARAMS_REQUIRED,
                               [](const std::string& format, absl::optional<size_t> max_length) {
                                 std::string filter_namespace;
                                 std::vector<std::string> path;

                                 CommonFormatterUtil::parseSubcommand(format, ':', filter_namespace,
                                                                      path);
                                 return std::make_unique<DynamicMetadataFieldExtractor>(
                                     filter_namespace, path, max_length);
                               }}},

                             {"CLUSTER_METADATA",
                              {CommandSyntaxChecker::PARAMS_REQUIRED,
                               [](const std::string& format, absl::optional<size_t> max_length) {
                                 std::string filter_namespace;
                                 std::vector<std::string> path;

                                 CommonFormatterUtil::parseSubcommand(format, ':', filter_namespace,
                                                                      path);
                                 return std::make_unique<ClusterMetadataFieldExtractor>(
                                     filter_namespace, path, max_length);
                               }}},
                             {"UPSTREAM_METADATA",
                              {CommandSyntaxChecker::PARAMS_REQUIRED,
                               [](const std::string& format, absl::optional<size_t> max_length) {
                                 std::string filter_namespace;
                                 std::vector<std::string> path;

                                 CommonFormatterUtil::parseSubcommand(format, ':', filter_namespace,
                                                                      path);
                                 return std::make_unique<UpstreamHostMetadataFieldExtractor>(
                                     filter_namespace, path, max_length);
                               }}},
                             {"FILTER_STATE",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL |
                                   CommandSyntaxChecker::LENGTH_ALLOWED,
                               [](const std::string& format, absl::optional<size_t> max_length) {
                                 return FilterStateFieldExtractor::create(format, max_length,
                                                                          false);
                               }}},
                             {"UPSTREAM_FILTER_STATE",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL |
                                   CommandSyntaxChecker::LENGTH_ALLOWED,
                               [](const std::string& format, absl::optional<size_t> max_length) {
                                 return FilterStateFieldExtractor::create(format, max_length, true);
                               }}},
                             {"DOWNSTREAM_PEER_CERT_V_START",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL,
                               [](const std::string& format, absl::optional<size_t>) {
                                 return std::make_unique<DownstreamPeerCertVStartFieldExtractor>(
                                     format);
                               }}},
                             {"DOWNSTREAM_PEER_CERT_V_END",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL,
                               [](const std::string& format, absl::optional<size_t>) {
                                 return std::make_unique<DownstreamPeerCertVEndFieldExtractor>(
                                     format);
                               }}},
                             {"UPSTREAM_PEER_CERT_V_START",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL,
                               [](const std::string& format, absl::optional<size_t>) {
                                 return std::make_unique<UpstreamPeerCertVStartFieldExtractor>(
                                     format);
                               }}},
                             {"UPSTREAM_PEER_CERT_V_END",
                              {CommandSyntaxChecker::PARAMS_OPTIONAL,
                               [](const std::string& format, absl::optional<size_t>) {
                                 return std::make_unique<UpstreamPeerCertVEndFieldExtractor>(
                                     format);
                               }}},
                             {"ENVIRONMENT",
                              {CommandSyntaxChecker::PARAMS_REQUIRED |
                                   CommandSyntaxChecker::LENGTH_ALLOWED,
                               [](const std::string& key, absl::optional<size_t> max_length) {
                                 return std::make_unique<EnvironmentFieldExtractor>(key,
                                                                                    max_length);
                               }}},
                         });
}

} // namespace StreamInfoOnly

PlainStringFormatter::PlainStringFormatter(const std::string& str) { str_.set_string_value(str); }

absl::optional<std::string>
PlainStringFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                             const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                             absl::string_view, AccessLog::AccessLogType) const {
  return str_.string_value();
}

ProtobufWkt::Value
PlainStringFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                  absl::string_view, AccessLog::AccessLogType) const {
  return str_;
}

PlainNumberFormatter::PlainNumberFormatter(double num) { num_.set_number_value(num); }

absl::optional<std::string>
PlainNumberFormatter::format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                             const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                             absl::string_view, AccessLog::AccessLogType) const {
  std::string str = absl::StrFormat("%g", num_.number_value());
  return str;
}

ProtobufWkt::Value
PlainNumberFormatter::formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                  absl::string_view, AccessLog::AccessLogType) const {
  return num_;
}

StreamInfoFormatter::StreamInfoFormatter(const std::string& command, const std::string& subcommand,
                                         absl::optional<size_t> length) {
  const StreamInfoOnly::FieldExtractorLookupTbl& extractors =
      StreamInfoOnly::getKnownFieldExtractors();

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

absl::optional<std::string> StreamInfoFormatter::format(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
    const StreamInfo::StreamInfo& stream_info, absl::string_view, AccessLog::AccessLogType) const {
  return field_extractor_->extract(stream_info);
}

ProtobufWkt::Value StreamInfoFormatter::formatValue(
    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&,
    const StreamInfo::StreamInfo& stream_info, absl::string_view, AccessLog::AccessLogType) const {
  return field_extractor_->extractValue(stream_info);
}

} // namespace Formatter
} // namespace Envoy
