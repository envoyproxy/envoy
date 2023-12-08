#include "source/common/formatter/stream_info_formatter.h"

#include <regex>

#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Formatter {

namespace {

static const std::string DefaultUnspecifiedValueString = "-";

const std::regex& getSystemTimeFormatNewlinePattern() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "%[-_0^#]*[1-9]*(E|O)?n");
}

} // namespace

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
  SubstitutionFormatUtils::truncate(str, max_length_);
  return str;
}

ProtobufWkt::Value
MetadataFormatter::formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const {
  if (path_.empty()) {
    const auto filter_it = metadata.filter_metadata().find(filter_namespace_);
    if (filter_it == metadata.filter_metadata().end()) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
    ProtobufWkt::Value output;
    output.mutable_struct_value()->CopyFrom(filter_it->second);
    return output;
  }

  const ProtobufWkt::Value& val =
      Config::Metadata::metadataValue(&metadata, filter_namespace_, path_);
  if (val.kind_case() == ProtobufWkt::Value::KindCase::KIND_NOT_SET) {
    return SubstitutionFormatUtils::unspecifiedValue();
  }

  return val;
}

absl::optional<std::string>
MetadataFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  auto metadata = get_func_(stream_info);
  return (metadata != nullptr) ? formatMetadata(*metadata) : absl::nullopt;
}

ProtobufWkt::Value MetadataFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
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
  std::string key, serialize_type, field_name;
  static constexpr absl::string_view PLAIN_SERIALIZATION{"PLAIN"};
  static constexpr absl::string_view TYPED_SERIALIZATION{"TYPED"};
  static constexpr absl::string_view FIELD_SERIALIZATION{"FIELD"};

  SubstitutionFormatUtils::parseSubcommand(format, ':', key, serialize_type, field_name);
  if (key.empty()) {
    throwEnvoyExceptionOrPanic("Invalid filter state configuration, key cannot be empty.");
  }

  if (serialize_type.empty()) {
    serialize_type = std::string(TYPED_SERIALIZATION);
  }
  if (serialize_type != PLAIN_SERIALIZATION && serialize_type != TYPED_SERIALIZATION &&
      serialize_type != FIELD_SERIALIZATION) {
    throwEnvoyExceptionOrPanic("Invalid filter state serialize type, only "
                               "support PLAIN/TYPED/FIELD.");
  }
  if ((serialize_type == FIELD_SERIALIZATION) ^ !field_name.empty()) {
    throwEnvoyExceptionOrPanic("Invalid filter state serialize type, FIELD "
                               "should be used with the field name.");
  }

  const bool serialize_as_string = serialize_type == PLAIN_SERIALIZATION;

  return std::make_unique<FilterStateFormatter>(key, max_length, serialize_as_string, is_upstream,
                                                field_name);
}

FilterStateFormatter::FilterStateFormatter(const std::string& key,
                                           absl::optional<size_t> max_length,
                                           bool serialize_as_string, bool is_upstream,
                                           const std::string& field_name)
    : key_(key), max_length_(max_length), is_upstream_(is_upstream) {
  if (!field_name.empty()) {
    format_ = FilterStateFormat::Field;
    field_name_ = field_name;
    factory_ = Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(key);
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
    if (!factory_) {
      return absl::nullopt;
    }
    const auto reflection = factory_->reflect(state);
    if (!reflection) {
      return absl::nullopt;
    }
    auto field_value = reflection->getField(field_name_);
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

ProtobufWkt::Value
FilterStateFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
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
    ProtobufWkt::Value val;
    if (MessageUtil::jsonConvertValue(*proto, val)) {
      return val;
    }
#endif
    return SubstitutionFormatUtils::unspecifiedValue();
  }
  case FilterStateFormat::Field: {
    if (!factory_) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
    const auto reflection = factory_->reflect(state);
    if (!reflection) {
      return SubstitutionFormatUtils::unspecifiedValue();
    }
    auto field_value = reflection->getField(field_name_);
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
    throwEnvoyExceptionOrPanic("Invalid header configuration. Format string contains newline.");
  }
}

absl::optional<std::string>
SystemTimeFormatter::format(const StreamInfo::StreamInfo& stream_info) const {
  const auto time_field = (*time_field_extractor_)(stream_info);
  if (!time_field.has_value()) {
    return absl::nullopt;
  }
  if (date_formatter_.formatString().empty()) {
    return AccessLogDateTimeFormatter::fromTime(time_field.value());
  }
  return date_formatter_.fromTime(time_field.value());
}

ProtobufWkt::Value
SystemTimeFormatter::formatValue(const StreamInfo::StreamInfo& stream_info) const {
  return ValueUtil::optionalStringValue(format(stream_info));
}

EnvironmentFormatter::EnvironmentFormatter(const std::string& key,
                                           absl::optional<size_t> max_length) {
  ASSERT(!key.empty());

  const char* env_value = std::getenv(key.c_str());
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
ProtobufWkt::Value EnvironmentFormatter::formatValue(const StreamInfo::StreamInfo&) const {
  return str_;
}

// StreamInfo std::string formatter provider.
class StreamInfoStringFormatterProvider : public StreamInfoFormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  StreamInfoStringFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // StreamInfoFormatterProvider
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }
  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
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
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    const auto millis = extractMillis(stream_info);
    if (!millis) {
      return absl::nullopt;
    }

    return fmt::format_int(millis.value()).str();
  }
  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
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
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    return fmt::format_int(field_extractor_(stream_info)).str();
  }
  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::numberValue(field_extractor_(stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

// StreamInfo Envoy::Network::Address::InstanceConstSharedPtr field extractor.
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
  absl::optional<std::string> format(const StreamInfo::StreamInfo& stream_info) const override {
    Network::Address::InstanceConstSharedPtr address = field_extractor_(stream_info);
    if (!address) {
      return absl::nullopt;
    }

    return toString(*address);
  }
  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
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

  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
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

  ProtobufWkt::Value formatValue(const StreamInfo::StreamInfo& stream_info) const override {
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

const StreamInfoFormatterProviderLookupTable& getKnownStreamInfoFormatterProviders() {
  CONSTRUCT_ON_FIRST_USE(
      StreamInfoFormatterProviderLookupTable,
      {
          {"REQUEST_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamRxByteReceived();
                  });
            }}},
          {"REQUEST_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastUpstreamTxByteSent();
                  });
            }}},
          {"RESPONSE_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.firstUpstreamRxByteReceived();
                  });
            }}},
          {"RESPONSE_TX_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.downstreamHandshakeComplete();
                  });
            }}},
          {"ROUNDTRIP_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    StreamInfo::TimingUtility timing(stream_info);
                    return timing.lastDownstreamAckReceived();
                  });
            }}},
          {"BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesReceived();
                  });
            }}},
          {"BYTES_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesRetransmitted();
                  });
            }}},
          {"PACKETS_RETRANSMITTED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.packetsRetransmitted();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesReceived() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_RECEIVED",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesReceived() : 0;
                  });
            }}},
          {"PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return SubstitutionFormatUtils::protocolToString(stream_info.protocol());
                  });
            }}},
          {"UPSTREAM_PROTOCOL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.responseCode().value_or(0);
                  });
            }}},
          {"RESPONSE_CODE_DETAILS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.responseCodeDetails();
                  });
            }}},
          {"CONNECTION_TERMINATION_DETAILS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.connectionTerminationDetails();
                  });
            }}},
          {"BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.bytesSent();
                  });
            }}},
          {"UPSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"UPSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getUpstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_WIRE_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->wireBytesSent() : 0;
                  });
            }}},
          {"DOWNSTREAM_HEADER_BYTES_SENT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    const auto& bytes_meter = stream_info.getDownstreamBytesMeter();
                    return bytes_meter ? bytes_meter->headerBytesSent() : 0;
                  });
            }}},
          {"DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoDurationFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.currentDuration();
                  });
            }}},
          {"RESPONSE_FLAGS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toShortString(stream_info);
                  });
            }}},
          {"RESPONSE_FLAGS_LONG",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return StreamInfo::ResponseFlagUtils::toString(stream_info);
                  });
            }}},
          {"UPSTREAM_HOST",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
                      return stream_info.upstreamInfo()->upstreamHost()->address();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_CLUSTER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
          {"UPSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo().has_value()) {
                      return stream_info.upstreamInfo().value().get().upstreamLocalAddress();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
                      return stream_info.upstreamInfo()->upstreamHost()->address();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
                      return stream_info.upstreamInfo()->upstreamHost()->address();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const StreamInfo::StreamInfo& stream_info)
                      -> std::shared_ptr<const Envoy::Network::Address::Instance> {
                    if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
                      return stream_info.upstreamInfo()->upstreamHost()->address();
                    }
                    return nullptr;
                  });
            }}},
          {"UPSTREAM_REQUEST_ATTEMPT_COUNT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.attemptCount().value_or(0);
                  });
            }}},
          {"UPSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"UPSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"UPSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"UPSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"UPSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUpstreamSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().localAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().remoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::withoutPort(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"DOWNSTREAM_DIRECT_REMOTE_PORT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return StreamInfoAddressFormatterProvider::justPort(
                  [](const Envoy::StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().directRemoteAddress();
                  });
            }}},
          {"CONNECTION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoUInt64FormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    return stream_info.downstreamAddressProvider().connectionID().value_or(0);
                  });
            }}},
          {"REQUESTED_SERVER_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().requestedServerName().empty()) {
                      result = std::string(
                          stream_info.downstreamAddressProvider().requestedServerName());
                    }
                    return result;
                  });
            }}},
          {"ROUTE_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
          {"DOWNSTREAM_PEER_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansPeerCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_URI_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.uriSanLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_DNS_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.dnsSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_LOCAL_IP_SAN",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return absl::StrJoin(connection_info.ipSansLocalCertificate(), ",");
                  });
            }}},
          {"DOWNSTREAM_PEER_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_LOCAL_SUBJECT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.subjectLocalCertificate();
                  });
            }}},
          {"DOWNSTREAM_TLS_SESSION_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sessionId();
                  });
            }}},
          {"DOWNSTREAM_TLS_CIPHER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.ciphersuiteString();
                  });
            }}},
          {"DOWNSTREAM_TLS_VERSION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.tlsVersion();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_256",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha256PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_FINGERPRINT_1",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.sha1PeerCertificateDigest();
                  });
            }}},
          {"DOWNSTREAM_PEER_SERIAL",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.serialNumberPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_ISSUER",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.issuerPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_PEER_CERT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoSslConnectionInfoFormatterProvider>(
                  [](const Ssl::ConnectionInfo& connection_info) {
                    return connection_info.urlEncodedPemEncodedPeerCertificate();
                  });
            }}},
          {"DOWNSTREAM_TRANSPORT_FAILURE_REASON",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string&, absl::optional<size_t>) {
              absl::optional<std::string> hostname = SubstitutionFormatUtils::getHostname();
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [hostname](const StreamInfo::StreamInfo&) { return hostname; });
            }}},
          {"FILTER_CHAIN_NAME",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                    return stream_info.virtualClusterName();
                  });
            }}},
          {"TLS_JA3_FINGERPRINT",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
              return std::make_unique<StreamInfoStringFormatterProvider>(
                  [](const StreamInfo::StreamInfo& stream_info) {
                    absl::optional<std::string> result;
                    if (!stream_info.downstreamAddressProvider().ja3Hash().empty()) {
                      result = std::string(stream_info.downstreamAddressProvider().ja3Hash());
                    }
                    return result;
                  });
            }}},
          {"STREAM_ID",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, absl::optional<size_t>) {
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
            [](const std::string& format, absl::optional<size_t>) {
              return std::make_unique<SystemTimeFormatter>(
                  format,
                  std::make_unique<SystemTimeFormatter::TimeFieldExtractor>(
                      [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<SystemTime> {
                        return stream_info.startTime();
                      }));
            }}},
          {"DYNAMIC_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](const std::string& format, absl::optional<size_t> max_length) {
              std::string filter_namespace;
              std::vector<std::string> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<DynamicMetadataFormatter>(filter_namespace, path, max_length);
            }}},

          {"CLUSTER_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](const std::string& format, absl::optional<size_t> max_length) {
              std::string filter_namespace;
              std::vector<std::string> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<ClusterMetadataFormatter>(filter_namespace, path, max_length);
            }}},
          {"UPSTREAM_METADATA",
           {CommandSyntaxChecker::PARAMS_REQUIRED,
            [](const std::string& format, absl::optional<size_t> max_length) {
              std::string filter_namespace;
              std::vector<std::string> path;

              SubstitutionFormatUtils::parseSubcommand(format, ':', filter_namespace, path);
              return std::make_unique<UpstreamHostMetadataFormatter>(filter_namespace, path,
                                                                     max_length);
            }}},
          {"FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](const std::string& format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, false);
            }}},
          {"UPSTREAM_FILTER_STATE",
           {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](const std::string& format, absl::optional<size_t> max_length) {
              return FilterStateFormatter::create(format, max_length, true);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](const std::string& format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVStartFormatter>(format);
            }}},
          {"DOWNSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](const std::string& format, absl::optional<size_t>) {
              return std::make_unique<DownstreamPeerCertVEndFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_START",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](const std::string& format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVStartFormatter>(format);
            }}},
          {"UPSTREAM_PEER_CERT_V_END",
           {CommandSyntaxChecker::PARAMS_OPTIONAL,
            [](const std::string& format, absl::optional<size_t>) {
              return std::make_unique<UpstreamPeerCertVEndFormatter>(format);
            }}},
          {"ENVIRONMENT",
           {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED,
            [](const std::string& key, absl::optional<size_t> max_length) {
              return std::make_unique<EnvironmentFormatter>(key, max_length);
            }}},
          {"UPSTREAM_CONNECTION_POOL_READY_DURATION",
           {CommandSyntaxChecker::COMMAND_ONLY,
            [](const std::string&, const absl::optional<size_t>&) {
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

} // namespace Formatter
} // namespace Envoy
