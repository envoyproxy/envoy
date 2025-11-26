#include "source/common/formatter/stream_info_formatter.h"

#include "source/common/common/random_generator.h"
#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "re2/re2.h"

namespace Envoy {
namespace Formatter {

namespace {

static const std::string DefaultUnspecifiedValueString = "-";

const re2::RE2& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(re2::RE2, "%[-_0^#]*[1-9]*(E|O)?n");
}

Network::Address::InstanceConstSharedPtr
getUpstreamRemoteAddress(const StreamInfo::StreamInfo& stream_info) {
  auto opt_ref = stream_info.upstreamInfo();
  if (!opt_ref.has_value()) {
    return nullptr;
  }

  if (auto addr = opt_ref->upstreamRemoteAddress(); addr != nullptr) {
    return addr;
  }
  return nullptr;
}

} // namespace

MetadataFormatter::MetadataFormatter(absl::string_view filter_namespace,
                                     const std::vector<absl::string_view>& path,
                                     absl::optional<size_t> max_length,
                                     MetadataFormatter::GetMetadataFunction get_func)
    : filter_namespace_(filter_namespace), path_(path.begin(), path.end()), max_length_(max_length),
      get_func_(get_func) {}

absl::optional<std::string>
MetadataFormatter::formatMetadata(const envoy::config::core::v3::Metadata& metadata) const {
  Protobuf::Value value = formatMetadataValue(metadata);
  if (value.kind_case() == Protobuf::Value::kNullValue) {
    return absl::nullopt;
  }

  std::string str;
  str.reserve(256);
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    str = value.string_value();
  } else {
    Json::Utility::appendValueToString(value, str);
  }
  SubstitutionFormatUtils::truncate(str, max_length_);
  return str;
}

Protobuf::Value
MetadataFormatter::formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const {
  if (path_.empty()) {
    const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
    if (filter_it == metadata.filter_metadata().end()) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
    Protobuf::Value output;
    output.mutable_struct_value()->CopyFrom(filter_it->second);
    return output;
  }

  const Protobuf::Value& val = Config::Metadata::metadataValue(&metadata, filter_namespace_, path_);
  if (val.kind_case() == Protobuf::Value::KindCase::KIND_NOT_SET) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  return val;
}

absl::optional<std::string>
MetadataFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  auto metadata = get_func_(stream_info);
  return (metadata != nullptr) ? formatMetadata(*metadata) : absl::nullopt;
}

Protobuf::Value MetadataFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
  auto metadata = get_func_(stream_info);
  return formatMetadataValue((metadata != nullptr) ? *metadata
                                                   : envoy::config::core::v3::Metadata());
}

// TODO(glicht): Consider adding support for route/listener/cluster metadata as suggested by
// @htuch. See: https://github.com/envoyproxy/envoy/issues/3006
DynamicMetadataFormatter::DynamicMetadataFormatter(absl::string_view filter_namespace,
                                                   const std::vector<absl::string_view>& path,
                                                   absl::optional<size_t> max_length)
    : MetadataFormatter(filter_namespace, path, max_length,
                        [](const StreamInfo::StreamInfo& stream_info) {
                          return &stream_info.dynamicMetadata();
                        }) {}

ClusterMetadataFormatter::ClusterMetadataFormatter(absl::string_view filter_namespace,
                                                   const std::vector<absl::string_view>& path,
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

UpstreamHostMetadataFormatter::UpstreamHostMetadataFormatter(
    absl::string_view filter_namespace, const std::vector<absl::string_view>& path,
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
FilterStateFormatter::create(absl::string_view format, absl::optional<size_t> max_length,
                             bool is_upstream) {
  absl::string_view key, serialize_type, field_name;
  static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
  static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};
  static constexpr absl::string_view FIELD_SERIALIZATION{"FIELD"};

  SubstitutionFormatUtils::parseSubcommand(format, ':', key, serialize_type, field_name);
  if (key.empty()) {
    throw EnvoyException("Invalid filter state configuration, key cannot be empty.");
  }

  if (serialize_type.empty()) {
    serialize_type = TYPED_SERIALIZATION;
  }
  if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION &&
      serialize_type != FIELD_SERIALIZATION) {
    throw EnvoyException("Invalid filter state serialize type, only "
                         "support PLAIN/TYPED/FIELD.");
  }
  if ((serialize_type == FIELD_SERIALIZATION) ^ !field_name.empty()) {
    throw EnvoyException("Invalid filter state serialize type, FIELD "
                         "should be used with the field name.");
  }

  const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

  return std::make_unique<FilterStateFormatter>(key, max_length, serialize_as_string, is_upstream,
                                                field_name);
}

FilterStateFormatter::FilterStateFormatter(absl::string_view key, absl::optional<size_t> max_length,
                                           bool serialize_as_string, bool is_upstream,
                                           absl::string_view field_name)
    : key_(key), max_length_(max_length), is_upstream_(is_upstream) {
  if (!field_name.empty()) {
    format_ = FilterStateFormat::Field;
    field_name_ = std::string(field_name);
  } else if (serialize_as_string) {
    format_ = FilterStateFormat::String;
  } else {
    format_ = FilterStateFormat::Proto;
  }
}

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

struct StringFieldVisitor {
  absl::optional<std::string> operator()(int64_t val) { return absl::StrCat(val); }
  absl::optional<std::string> operator()(absl::string_view val) { return std::string(val); }
  absl::optional<std::string> operator()(absl::monostate) { return {}; }
};

absl::optional<std::string>
FilterStateFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
  if (!state) {
    return absl::nullopt;
  }

  switch (format_) {
  case FilterStateFormat::String: {
    absl::optional<std::string> plain_value = state->serializeAsString();
    if (plain_value.has_value()) {
      SubstitutionFormatUtils::truncate(plain_value.value(), max_length_);
      return plain_value.value();
    }
    return absl::nullopt;
  }
  case FilterStateFormat::Proto: {
    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (proto == nullptr) {
      return absl::nullopt;
    }

#if defined(ENVOY_ENABLE_FULL_PROTOS)
    std::string value;
    const auto status = Protobuf::util::MessageToJsonString(*proto, &value);
    if (!status.ok()) {
      // If the message contains an unknown Any (from WASM or Lua), MessageToJsonString will fail.
      // TODO(lizan): add support of unknown Any.
      return absl::nullopt;
    }

    SubstitutionFormatUtils::truncate(value, max_length_);
    return value;
#else
    PANIC("FilterStateFormatter::format requires full proto support");
    return absl::nullopt;
#endif
  }
  case FilterStateFormat::Field: {
    auto field_value = state->getField(field_name_);
    auto string_value = absl::visit(StringFieldVisitor(), field_value);
    if (!string_value) {
      return absl::nullopt;
    }
    SubstitutionFormatUtils::truncate(string_value.value(), max_length_);
    return string_value;
  }
  default:
    return absl::nullopt;
  }
}

Protobuf::Value FilterStateFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
  const Envoy::StreamInfo::FilterState::Object* state = filterState(stream_info);
  if (!state) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  switch (format_) {
  case FilterStateFormat::String: {
    absl::optional<std::string> plain_value = state->serializeAsString();
    if (plain_value.has_value()) {
      SubstitutionFormatUtils::truncate(plain_value.value(), max_length_);
      return ValueUtil::stringValue(plain_value.value());
    }
    return SubstitutionFormatUtils::unspecifiedValue();
  }
  case FilterStateFormat::Proto: {
    ProtobufTypes::MessagePtr proto = state->serializeAsProto();
    if (!proto) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

#ifdef ENVOY_ENABLE_YAML
    Protobuf::Value val;
    if (MessageUtil::jsonConvertValue(*proto, val)) {
      return val;
    }
#endif
    return SubstitutionFormatUtils::unspecifiedValue();
  }
  case FilterStateFormat::Field: {
    auto field_value = state->getField(field_name_);
    auto string_value = absl::visit(StringFieldVisitor(), field_value);
    if (!string_value) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
    SubstitutionFormatUtils::truncate(string_value.value(), max_length_);
    return ValueUtil::stringValue(string_value.value());
  }
  default:
    return SubstitutionFormatUtils::unspecifiedValue();
  }
}

const absl::flat_hash_map<absl::string_view, CommonDurationFormatter::TimePointGetter>
    CommonDurationFormatter::KnownTimePointGetters{
        {FirstDownstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           return stream_info.startTimeMonotonic();
         }},
        {LastDownstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->lastDownstreamRxByteReceived();
           }
           return {};
         }},
        {UpstreamConnectStart,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().upstream_connect_start_;
           }
           return {};
         }},
        {UpstreamConnectEnd,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().upstream_connect_complete_;
           }
           return {};
         }},
        {UpstreamTLSConnectEnd,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().upstream_handshake_complete_;
           }
           return {};
         }},
        {FirstUpstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().first_upstream_tx_byte_sent_;
           }
           return {};
         }},
        {LastUpstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().last_upstream_tx_byte_sent_;
           }
           return {};
         }},
        {FirstUpstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().first_upstream_rx_byte_received_;
           }
           return {};
         }},
        {FirstUpstreamRxBodyReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().first_upstream_rx_body_byte_received_;
           }
           return {};
         }},
        {LastUpstreamRxByteReceived,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto upstream_info = stream_info.upstreamInfo();
           if (upstream_info.has_value()) {
             return upstream_info->upstreamTiming().last_upstream_rx_byte_received_;
           }
           return {};
         }},
        {FirstDownstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->firstDownstreamTxByteSent();
           }
           return {};
         }},
        {LastDownstreamTxByteSent,
         [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<MonotonicTime> {
           const auto downstream_timing = stream_info.downstreamTiming();
           if (downstream_timing.has_value()) {
             return downstream_timing->lastDownstreamTxByteSent();
           }
           return {};
         }},
    };

CommonDurationFormatter::TimePointGetter
CommonDurationFormatter::getTimePointGetterByName(absl::string_view name) {
  auto it = KnownTimePointGetters.find(name);
  if (it != KnownTimePointGetters.end()) {
    return it->second;
  }

  return [key = std::string(name)](const StreamInfo::StreamInfo& info) {
    const auto downstream_timing = info.downstreamTiming();
    if (downstream_timing.has_value()) {
      return downstream_timing->getValue(key);
    }
    return absl::optional<MonotonicTime>{};
  };
}

std::unique_ptr<CommonDurationFormatter>
CommonDurationFormatter::create(absl::string_view sub_command) {
  // Split the sub_command by ':'.
  absl::InlinedVector<absl::string_view, 3> parsed_sub_commands = absl::StrSplit(sub_command, ':');

  if (parsed_sub_commands.size() < 2 || parsed_sub_commands.size() > 3) {
    throw EnvoyException(fmt::format("Invalid common duration configuration: {}.", sub_command));
  }

  absl::string_view start = parsed_sub_commands[0];
  absl::string_view end = parsed_sub_commands[1];

  // Milliseconds is the default precision.
  DurationPrecision precision = DurationPrecision::Milliseconds;

  if (parsed_sub_commands.size() == 3) {
    absl::string_view precision_str = parsed_sub_commands[2];
    if (precision_str == MillisecondsPrecision) {
      precision = DurationPrecision::Milliseconds;
    } else if (precision_str == MicrosecondsPrecision) {
      precision = DurationPrecision::Microseconds;
    } else if (precision_str == NanosecondsPrecision) {
      precision = DurationPrecision::Nanoseconds;
    } else {
      throw EnvoyException(fmt::format("Invalid common duration precision: {}.", precision_str));
    }
  }

  TimePointGetter start_getter = getTimePointGetterByName(start);
  TimePointGetter end_getter = getTimePointGetterByName(end);

  return std::make_unique<CommonDurationFormatter>(std::move(start_getter), std::move(end_getter),
                                                   precision);
}

absl::optional<uint64_t>
CommonDurationFormatter::getDurationCount(const StreamInfo::StreamInfo& info) const {
  auto time_point_beg = time_point_beg_(info);
  auto time_point_end = time_point_end_(info);

  if (!time_point_beg.has_value() || !time_point_end.has_value()) {
    return absl::nullopt;
  }

  if (time_point_end.value() < time_point_beg.value()) {
    return absl::nullopt;
  }

  auto duration = time_point_end.value() - time_point_beg.value();

  switch (duration_precision_) {
  case DurationPrecision::Milliseconds:
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  case DurationPrecision::Microseconds:
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  case DurationPrecision::Nanoseconds:
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  }
  PANIC("Invalid duration precision");
}

absl::optional<std::string>
CommonDurationFormatter::format(const StreamInfo::StreamInfo& info) const {
  auto duration = getDurationCount(info);
  if (!duration.has_value()) {
    return absl::nullopt;
  }
  return fmt::format_int(duration.value()).str();
}
Protobuf::Value CommonDurationFormatter::formatValue(const StreamInfo::StreamInfo& info) const {
  auto duration = getDurationCount(info);
  if (!duration.has_value()) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }
  return ValueUtil::numberValue(duration.value());
}

// A SystemTime formatter that extracts the startTime from StreamInfo. Must be provided
// an access log command that starts with `START_TIME`.
StartTimeFormatter::StartTimeFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      })) {}

DownstreamPeerCertVStartFormatter::DownstreamPeerCertVStartFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->validFromPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
DownstreamPeerCertVEndFormatter::DownstreamPeerCertVEndFormatter(absl::string_view format)
    : SystemTimeFormatter(
          format, std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        const auto connection_info =
                            stream_info.downstreamAddressProvider().sslConnection();
                        return connection_info != nullptr
                                   ? connection_info->expirationPeerCertificate()
                                   : absl::optional<SystemTime>();
                      })) {}
UpstreamPeerCertVStartFormatter::UpstreamPeerCertVStartFormatter(absl::string_view format)
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
UpstreamPeerCertVEndFormatter::UpstreamPeerCertVEndFormatter(absl::string_view format)
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

SystemTimeFormatter::SystemTimeFormatter(absl::string_view format, TimeFieldExtractorPtr f,
                                         bool local_time)
    : date_formatter_(format, local_time), time_field_extractor_(std::move(f)),
      local_time_(local_time) {
  // Validate the input specifier here. The formatted string may be destined for a header, and
  // should not contain invalid characters {NUL, LR, CF}.
  if (re2::RE2::PartialMatch(format, getSystemTimeFormatNewlinePattern())) {
    throw EnvoyException("Invalid header configuration. Format string contains newline.");
  }
}

absl::optional<std::string>
SystemTimeFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  const auto time_field = (*time_field_extractor_)(stream_info);
  if (!time_field.has_value()) {
    return absl::nullopt;
  }
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(time_field.value(), local_time_);
  }
  return date_formatter_.fromTime(time_field.value());
}

Protobuf::Value SystemTimeFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
  return ValueUtil::optionalStringValue(format(stream_info));
}

EnvironmentFormatter::EnvironmentFormatter(absl::string_view key,
                                           absl::optional<size_t> max_length) {
  ASSERT(!key.empty());

  const std::string key_str = std::string(key);
  const char* env_value = std::getenv(key_str.c_str());
  if (env_value != nullptr) {
    std::string env_string = env_value;
    SubstitutionFormatUtils::truncate(env_string, max_length);
    str_.set_string_value(env_string);
    return;
  }
  str_.set_string_value(DefaultUnspecifiedValueString);
}

absl::optional<std::string> EnvironmentFormatter::format(const StreamInfo::StreamInfo&) const {
  return str_.string_value();
}
Protobuf::Value EnvironmentFormatter::formatValue(const StreamInfo::StreamInfo&) const {
  return str_;
}

// StreamInfo std::string formatter provider.
class StreamInfoStringFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }
  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo std::chrono_nanoseconds field extractor.
class StreamInfoDurationFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::chrono::nanoseconds>(const StreamInfo::StreamInfo&)>;

  StreamInfoDurationFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return absl::nullopt;
    }

    return fmt::format_int(millis.value()).str();
  }
  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return SubstitutionFormatUtils::unspecifiedValue();
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
class StreamInfoUInt64FormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor = std::function<uint64_t(const StreamInfo::StreamInfo&)>;

  StreamInfoUInt64FormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    return fmt::format_int(field_extractor_(stream_info)).str();
  }
  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::numberValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo Network::Address::InstanceConstSharedPtr field extractor.
class StreamInfoAddressFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<Network::Address::InstanceConstSharedPtr(const StreamInfo::StreamInfo&)>;

  static std::unique_ptr<StreamInfoAddressFormatterProvider> withPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::WithPort);
  }

  static std::unique_ptr<StreamInfoAddressFormatterProvider> withoutPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::WithoutPort);
  }

  static std::unique_ptr<StreamInfoAddressFormatterProvider> justPort(FieldExtractor f) {
    return std::make_unique<StreamInfoAddressFormatterProvider>(
        f, StreamInfoAddressFieldExtractionType::JustPort);
  }

  StreamInfoAddressFormatterProvider(FieldExtractor f,
                                     StreamInfoAddressFieldExtractionType extraction_type)
      : field_extractor_(f), extraction_type_(extraction_type) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return absl::nullopt;
    }

    return toString(*address);
  }
  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

    if (extraction_type_ == StreamInfoAddressFieldExtractionType::JustPort) {
      const auto port = StreamInfo::Utility::extractDownstreamAddressJustPort(*address);
      if (port) {
        return ValueUtil::numberValue(*port);
      }
      return SubstitutionFormatUtils::unspecifiedValue();
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
class StreamInfoSslConnectionInfoFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoSslConnectionInfoFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return absl::nullopt;
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return absl::nullopt;
    }

    return value;
  }

  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.downstreamAddressProvider().sslConnection() == nullptr) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

    const auto value = field_extractor_(*stream_info.downstreamAddressProvider().sslConnection());
    if (value && value->empty()) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

class StreamInfoUpstreamSslConnectionInfoFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const Ssl::ConnectionInfo& connection_info)>;

  StreamInfoUpstreamSslConnectionInfoFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  // Don't hide the other structure of format and formatValue.
  using StreamInfoFormatterProvider::format;
  using StreamInfoFormatterProvider::formatValue;
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
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

  Protobuf::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    if (!stream_info.upstreamInfo() ||
        stream_info.upstreamInfo()->upstreamSslConnection() == nullptr) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

    const auto value = field_extractor_(*(stream_info.upstreamInfo()->upstreamSslConnection()));
    if (value && value->empty()) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }

    return ValueUtil::optionalStringValue(value);
  }

private:
  FieldExtractor field_extractor_;
};

using StreamInfoFormatterProviderLookupTable =
    absl::flat_hash_map<absl::string_view, std::pair<CommandSyntaxChecker::CommandSyntaxFlags,
                                                     StreamInfoFormatterProviderCreateFunc>>;

const StreamInfoFormatterProviderLookupTable& getKnownStreamInfoFormatterProviders() {
  CONSTRUCT_ON_FIRST_USE(
      StreamInfoFormatterProviderLookupTable,
      {
          {"REQUEST_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamRxByteReceived();
                  });
            }}},
          {"REQUEST_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastUpstreamTxByteSent();
                  });
            }}},
          {"RESPONSE_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.firstUpstreamRxByteReceived();
                  });
            }}},
          {"RESPONSE_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
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
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.downstreamHandshakeComplete();
                  });
            }}},
          {"ROUNDTRIP_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamAckReceived();
                  });
            }}},
          {"BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesReceived();
                  });
            }}},
          {"BYTES_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesRetransmitted();
                  });
            }}},
          {"PACKETS_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.packetsRetransmitted();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"UPSTREAM_DECOMPRESSED_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->decompressedHeaderBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_DECOMPRESSED_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->decompressedHeaderBytesReceived() : 0;
                  });
            }}},
          {"PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return SubstitutionFormatUtils::protocolToString(stream_info.protocol());
                  });
            }}},
          {"UPSTREAM_PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.upstreamInfo()
                               ? SubstitutionFormatUtils::protocolToString(
                                     stream_info.upstreamInfo()->upstreamProtocol())
                               : absl::nullopt;
                  });
            }}},
          {"RESPONSE_CODE",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.responseCode().value_or(0);
                  });
            }}},
          {"RESPONSE_CODE_DETAILS",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              bool allow_whitespaces = (format == "ALLOW_WHITESPACES");
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [allow_whitespaces](const StreamInfo::StreamInfo& stream_info) {
                    if (allow_whitespaces || !stream_info.responseCodeDetails().has_value()) {
                      return stream_info.responseCodeDetails();
                    }
                    return absl::optional<std::string>(StringUtil::replaceAllEmptySpace(
                        stream_info.responseCodeDetails().value()));
                  });
            }}},
          {"CONNECTION_TERMINATION_DETAILS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.connectionTerminationDetails();
                  });
            }}},
          {"BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesSent();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"UPSTREAM_DECOMPRESSED_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->decompressedHeaderBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_DECOMPRESSED_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->decompressedHeaderBytesSent() : 0;
                  });
            }}},
          {"DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.currentDuration();
                  });
            }}},
          {"COMMON_DURATION",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view sub_command, absl::optional<size_t>) {
              return CommonDurationFormatter::create(sub_command);
            }}},
          {"CUSTOM_FLAGS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return std::string(stream_info.customFlags());
                  });
            }}},
          {"RESPONSE_FLAGS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
                  });
            }}},
          {"RESPONSE_FLAGS_LONG",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toString(stream_info);
                  });
            }}},
          {"UPSTREAM_HOST_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                    const auto opt_ref = stream_info.upstreamInfo();
                    if (!opt_ref.has_value()) {
                      return absl::nullopt;
                    }
                    const auto host = opt_ref->upstreamHost();
                    if (host == nullptr) {
                      return absl::nullopt;
                    }
                    std::string host_name = host->hostname();
                    if (host_name.empty()) {
                      // If no hostname is available, the main address is used.
                      return host->address()->asString();
                    }
                    return absl::make_optional<std::string>(std::move(host_name));
                  });
            }}},
          {"UPSTREAM_HOST_NAME_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                    const auto opt_ref = stream_info.upstreamInfo();
                    if (!opt_ref.has_value()) {
                      return absl::nullopt;
                    }
                    const auto host = opt_ref->upstreamHost();
                    if (host == nullptr) {
                      return absl::nullopt;
                    }
                    std::string host_name = host->hostname();
                    if (host_name.empty()) {
                      // If no hostname is available, the main address is used.
                      host_name = host->address()->asString();
                    }
                    Envoy::Http::HeaderUtility::stripPortFromHost(host_name);
                    return absl::make_optional<std::string>(std::move(host_name));
                  });
            }}},
          {"UPSTREAM_HOST",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    const auto opt_ref = stream_info.upstreamInfo();
                    if (!opt_ref.has_value()) {
                      return nullptr;
                    }
                    const auto host = opt_ref->upstreamHost();
                    if (host == nullptr) {
                      return nullptr;
                    }
                    return host->address();
                  });
            }}},
          {"UPSTREAM_CONNECTION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    uint64_t upstream_connection_id = 0;
                    if (stream_info.upstreamInfo().has_value()) {
                      upstream_connection_id =
                          stream_info.upstreamInfo()->upstreamConnectionId().value_or(0);
                    }
                    return upstream_connection_id;
                  });
            }}},
          {"UPSTREAM_CLUSTER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    std::string upstream_cluster_name;
                    if (stream_info.upstreamClusterInfo().has_value() &&
                        stream_info.upstreamClusterInfo().value() != nullptr) {
                      upstream_cluster_name =
                          stream_info.upstreamClusterInfo().value()->observabilityName();
                    }

                    return upstream_cluster_name.empty()
                               ? absl::nullopt
                               : absl::make_optional<std::string>(upstream_cluster_name);
                  });
            }}},
          {"UPSTREAM_CLUSTER_RAW",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    std::string upstream_cluster_name;
                    if (stream_info.upstreamClusterInfo().has_value() &&
                        stream_info.upstreamClusterInfo().value() != nullptr) {
                      upstream_cluster_name = stream_info.upstreamClusterInfo().value()->name();
                    }

                    return upstream_cluster_name.empty()
                               ? absl::nullopt
                               : absl::make_optional<std::string>(upstream_cluster_name);
                  });
            }}},
          {"UPSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> Network::Address::InstanceConstSharedPtr {
                    return getUpstreamRemoteAddress(stream_info);
                  });
            }}},
          {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.attemptCount().value_or(0);
                  });
            }}},
          {"UPSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"UPSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"UPSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"UPSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directLocalAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directLocalAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directLocalAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"CONNECTION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().connectionID().value_or(0);
                  });
            }}},
          {"REQUESTED_SERVER_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().requestedServerName().empty()) {
                      result = StringUtil::sanitizeInvalidHostname(
                          stream_info.downstreamAddressProvider().requestedServerName());
                    }
                    return result;
                  });
            }}},
          {"ROUTE_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    std::string route_name = stream_info.getRouteName();
                    if (!route_name.empty()) {
                      result = route_name;
                    }
                    return result;
                  });
            }}},
          {"UPSTREAM_PEER_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_PEER_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_PEER_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansPeerCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(), ",");
                  });
            }}},
          {"UPSTREAM_LOCAL_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_EMAIL_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.emailSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_OTHERNAME_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.othernameSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_EMAIL_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.emailSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_OTHERNAME_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.othernameSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectLocalCertificate();
                  });
            }}},
          {"DOWNSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"DOWNSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"DOWNSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_256",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha256PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_1",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha1PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_SERIAL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.serialNumberPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_256",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.sha256PeerCertificateChainDigests(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_1",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.sha1PeerCertificateChainDigests(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_CHAIN_SERIALS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.serialNumbersPeerCertificates(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_TRANSPORT_FAILURE_REASON",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamTransportFailureReason().empty()) {
                      result = absl::StrReplaceAll(stream_info.downstreamTransportFailureReason(),
                                                   {{" ", "_"}});
                    }
                    return result;
                  });
            }}},
          {"UPSTREAM_TRANSPORT_FAILURE_REASON",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (stream_info.upstreamInfo().has_value() &&
                        !stream_info.upstreamInfo()
                             .value()
                             .get()
                             .upstreamTransportFailureReason()
                             .empty()) {
                      result =
                          stream_info.upstreamInfo().value().get().upstreamTransportFailureReason();
                    }
                    if (result) {
                      std::replace(result->begin(), result->end(), ' ', '_');
                    }
                    return result;
                  });
            }}},
          {"HOSTNAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              absl::optional<std::string> hostname = SubstitutionFormatUtils::getHostname();
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [hostname](const StreamInfo::StreamInfo&) { return hostname; });
            }}},
          {"FILTER_CHAIN_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                    if (const auto info = stream_info.downstreamAddressProvider().filterChainInfo();
                        info.has_value()) {
                      if (!info->name().empty()) {
                        return std::string(info->name());
                      }
                    }
                    return absl::nullopt;
                  });
            }}},
          {"VIRTUAL_CLUSTER_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                    return stream_info.virtualClusterName();
                  });
            }}},
          {"TLS_JA3_FINGERPRINT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().ja3Hash().empty()) {
                      result = std::string(stream_info.downstreamAddressProvider().ja3Hash());
                    }
                    return result;
                  });
            }}},
          {"TLS_JA4_FINGERPRINT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().ja4Hash().empty()) {
                      result = std::string(stream_info.downstreamAddressProvider().ja4Hash());
                    }
                    return result;
                  });
            }}},
          {"UNIQUE_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, const absl::optional<size_t>&) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                    return absl::make_optional<std::string>(Random::RandomUtility::uuid());
                  });
            }}},
          {"STREAM_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
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
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      }));
            }}},
          {"START_TIME_LOCAL",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      }),
                  true);
            }}},
          {"EMIT_TIME",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.timeSource().systemTime();
                      }));
            }}},
          {"EMIT_TIME_LOCAL",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.timeSource().systemTime();
                      }),
                  true);
            }}},
          {"DYNAMIC_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
            }}},

          {"CLUSTER_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<ClusterMetadataFormatter>(filter_namespace, path, max_length);
            }}},
          {"UPSTREAM_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              absl::string_view filter_namespace;
              std::vector<absl::string_view> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<UpstreamHostMetadataFormatter>(filter_namespace, path,
                                                                     max_length);
            }}},
          {"FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, false);
            }}},
          {"UPSTREAM_FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, true);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVStartFormatter>(format);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVEndFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVStartFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](absl::string_view format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVEndFormatter>(format);
            }}},
          {"ENVIRONMENT",
           {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](absl::string_view key, absl::optional<size_t> max_length) {
              return std::make_unique<EnvironmentFormatter>(key, max_length);
            }}},
          {"UPSTREAM_CONNECTION_POOL_READY_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](absl::string_view, const absl::optional<size_t>&) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> absl::optional<std::chrono::nanoseconds> {
                    if (auto upstream_info = stream_info.upstreamInfo();
                        upstream_info.has_value()) {
                      if (auto connection_pool_callback_latency =
                              upstream_info.value()
                                  .get()
                                  .upstreamTiming()
                                  .connectionPoolCallbackLatency();
                          connection_pool_callback_latency.has_value()) {
                        return connection_pool_callback_latency;
                      }
                    }
                    return absl::nullopt;
                  });
            }}},
      });
}

class BuiltInStreamInfoCommandParser : public CommandParser {
public:
  BuiltInStreamInfoCommandParser() = default;

  // CommandParser
  FormatterProviderPtr parse(absl::string_view command, absl::string_view sub_command,
                             absl::optional<size_t> max_length) const override {

    auto it = getKnownStreamInfoFormatterProviders().find(command);

    // No throw because the stream info command parser may not be the last parser and other
    // formatter parsers may be tried.
    if (it == getKnownStreamInfoFormatterProviders().end()) {
      return nullptr;
    }
    // Check flags for the command.
    THROW_IF_NOT_OK(Envoy::Formatter::CommandSyntaxChecker::verifySyntax(
        (*it).second.first, command, sub_command, max_length));

    return (*it).second.second(sub_command, max_length);
  }
};

std::string DefaultBuiltInStreamInfoCommandParserFactory::name() const {
  return "envoy.built_in_formatters.stream_info.default";
}

CommandParserPtr DefaultBuiltInStreamInfoCommandParserFactory::createCommandParser() const {
  return std::make_unique<BuiltInStreamInfoCommandParser>();
}

REGISTER_FACTORY(DefaultBuiltInStreamInfoCommandParserFactory, BuiltInCommandParserFactory);

} // namespace Formatter
} // namespace Envoy
