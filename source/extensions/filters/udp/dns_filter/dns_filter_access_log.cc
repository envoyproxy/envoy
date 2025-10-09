#include "source/extensions/filters/udp/dns_filter/dns_filter_access_log.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

namespace {

constexpr absl::string_view DnsFilterMetadataNamespace = "envoy.filters.udp.dns_filter";

/**
 * FormatterProvider for DNS-specific string fields from dynamic metadata.
 */
class DnsStringFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor =
      std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  DnsStringFormatterProvider(FieldExtractor field_extractor)
      : field_extractor_(std::move(field_extractor)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const Formatter::Context&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }

  Protobuf::Value formatValueWithContext(const Formatter::Context&,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    const auto str = field_extractor_(stream_info);
    return str.has_value() ? ValueUtil::stringValue(str.value()) : ValueUtil::nullValue();
  }

private:
  const FieldExtractor field_extractor_;
};

/**
 * FormatterProvider for DNS-specific numeric fields from dynamic metadata.
 */
class DnsNumberFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<double>(const StreamInfo::StreamInfo&)>;

  DnsNumberFormatterProvider(FieldExtractor field_extractor)
      : field_extractor_(std::move(field_extractor)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const Formatter::Context&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    const auto num = field_extractor_(stream_info);
    return num.has_value() ? absl::StrCat(static_cast<uint64_t>(num.value())) : absl::nullopt;
  }

  Protobuf::Value formatValueWithContext(const Formatter::Context&,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    const auto num = field_extractor_(stream_info);
    return num.has_value() ? ValueUtil::numberValue(num.value()) : ValueUtil::nullValue();
  }

private:
  const FieldExtractor field_extractor_;
};

/**
 * FormatterProvider for DNS-specific boolean fields from dynamic metadata.
 */
class DnsBoolFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<bool>(const StreamInfo::StreamInfo&)>;

  DnsBoolFormatterProvider(FieldExtractor field_extractor)
      : field_extractor_(std::move(field_extractor)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const Formatter::Context&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    const auto val = field_extractor_(stream_info);
    return val.has_value() ? (val.value() ? "true" : "false") : absl::nullopt;
  }

  Protobuf::Value formatValueWithContext(const Formatter::Context&,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    const auto val = field_extractor_(stream_info);
    return val.has_value() ? ValueUtil::boolValue(val.value()) : ValueUtil::nullValue();
  }

private:
  const FieldExtractor field_extractor_;
};

/**
 * Helper to extract string field from DNS filter dynamic metadata.
 */
absl::optional<std::string> getDnsMetadataString(const StreamInfo::StreamInfo& stream_info,
                                                 absl::string_view field_name) {
  const auto& metadata = stream_info.dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(DnsFilterMetadataNamespace);
  if (filter_it == metadata.end()) {
    return absl::nullopt;
  }

  const auto& fields = filter_it->second.fields();
  const auto field_it = fields.find(field_name);
  if (field_it == fields.end()) {
    return absl::nullopt;
  }

  if (field_it->second.kind_case() == Protobuf::Value::kStringValue) {
    return field_it->second.string_value();
  }
  return absl::nullopt;
}

/**
 * Helper to extract number field from DNS filter dynamic metadata.
 */
absl::optional<double> getDnsMetadataNumber(const StreamInfo::StreamInfo& stream_info,
                                            absl::string_view field_name) {
  const auto& metadata = stream_info.dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(DnsFilterMetadataNamespace);
  if (filter_it == metadata.end()) {
    return absl::nullopt;
  }

  const auto& fields = filter_it->second.fields();
  const auto field_it = fields.find(field_name);
  if (field_it == fields.end()) {
    return absl::nullopt;
  }

  if (field_it->second.kind_case() == Protobuf::Value::kNumberValue) {
    return field_it->second.number_value();
  }
  return absl::nullopt;
}

/**
 * Helper to extract boolean field from DNS filter dynamic metadata.
 */
absl::optional<bool> getDnsMetadataBool(const StreamInfo::StreamInfo& stream_info,
                                        absl::string_view field_name) {
  const auto& metadata = stream_info.dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(DnsFilterMetadataNamespace);
  if (filter_it == metadata.end()) {
    return absl::nullopt;
  }

  const auto& fields = filter_it->second.fields();
  const auto field_it = fields.find(field_name);
  if (field_it == fields.end()) {
    return absl::nullopt;
  }

  if (field_it->second.kind_case() == Protobuf::Value::kBoolValue) {
    return field_it->second.bool_value();
  }
  return absl::nullopt;
}

/**
 * DNS Filter command parser implementation.
 */
class DnsFilterCommandParser : public Formatter::CommandParser {
public:
  using ProviderFunc = std::function<Formatter::FormatterProviderPtr(absl::string_view,
                                                                      absl::optional<size_t>)>;
  using ProviderFuncTable = absl::flat_hash_map<std::string, ProviderFunc>;

  // CommandParser
  Formatter::FormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                                        absl::optional<size_t> max_length) const override {
    const auto& provider_table = providerFuncTable();
    const auto func_it = provider_table.find(std::string(command));
    if (func_it == provider_table.end()) {
      return nullptr;
    }
    return func_it->second(command_arg, max_length);
  }

private:
  static const ProviderFuncTable& providerFuncTable() {
    CONSTRUCT_ON_FIRST_USE(
        ProviderFuncTable,
        {
            {"QUERY_NAME",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsStringFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<std::string> {
                     return getDnsMetadataString(stream_info, "query_name");
                   });
             }},
            {"QUERY_TYPE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsNumberFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<double> {
                     return getDnsMetadataNumber(stream_info, "query_type");
                   });
             }},
            {"QUERY_CLASS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsNumberFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<double> {
                     return getDnsMetadataNumber(stream_info, "query_class");
                   });
             }},
            {"ANSWER_COUNT",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsNumberFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<double> {
                     return getDnsMetadataNumber(stream_info, "answer_count");
                   });
             }},
            {"RESPONSE_CODE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsNumberFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<double> {
                     return getDnsMetadataNumber(stream_info, "response_code");
                   });
             }},
            {"PARSE_STATUS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return std::make_unique<DnsBoolFormatterProvider>(
                   [](const StreamInfo::StreamInfo& stream_info) -> absl::optional<bool> {
                     return getDnsMetadataBool(stream_info, "parse_status");
                   });
             }},
        });
  }
};

} // namespace

Formatter::CommandParserPtr createDnsFilterCommandParser() {
  return std::make_unique<DnsFilterCommandParser>();
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
