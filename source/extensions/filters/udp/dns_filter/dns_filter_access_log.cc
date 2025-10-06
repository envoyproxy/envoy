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
 * FormatterProvider for DNS-specific fields from dynamic metadata.
 * All DNS fields are stored as strings in metadata for simpler formatting.
 */
class DnsFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const StreamInfo::StreamInfo&)>;

  DnsFormatterProvider(FieldExtractor field_extractor)
      : field_extractor_(std::move(field_extractor)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const Formatter::Context&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(stream_info);
  }

  ProtobufWkt::Value
  formatValueWithContext(const Formatter::Context&,
                         const StreamInfo::StreamInfo& stream_info) const override {
    const auto str = field_extractor_(stream_info);
    return str.has_value() ? ValueUtil::stringValue(str.value()) : ValueUtil::nullValue();
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

  if (field_it->second.kind_case() == ProtobufWkt::Value::kStringValue) {
    return field_it->second.string_value();
  }
  return absl::nullopt;
}

/**
 * Helper to create a provider function for a given DNS metadata field name.
 */
Formatter::FormatterProviderPtr makeDnsFieldProvider(absl::string_view field_name) {
  return std::make_unique<DnsFormatterProvider>(
      [field_name = std::string(field_name)](const StreamInfo::StreamInfo& stream_info)
          -> absl::optional<std::string> { return getDnsMetadataString(stream_info, field_name); });
}

/**
 * DNS Filter command parser implementation.
 */
class DnsFilterCommandParser : public Formatter::CommandParser {
public:
  using ProviderFunc =
      std::function<Formatter::FormatterProviderPtr(absl::string_view, absl::optional<size_t>)>;
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
               return makeDnsFieldProvider("query_name");
             }},
            {"QUERY_TYPE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeDnsFieldProvider("query_type");
             }},
            {"QUERY_CLASS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeDnsFieldProvider("query_class");
             }},
            {"ANSWER_COUNT",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeDnsFieldProvider("answer_count");
             }},
            {"RESPONSE_CODE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeDnsFieldProvider("response_code");
             }},
            {"PARSE_STATUS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeDnsFieldProvider("parse_status");
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
