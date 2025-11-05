#include "source/extensions/filters/udp/dns_filter/dns_filter_access_log.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/udp/dns_filter/dns_parser.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

namespace {

/**
 * FormatterProvider for DNS-specific fields from DnsQueryContext.
 */
class DnsFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor = std::function<absl::optional<std::string>(const Formatter::Context&,
                                                                   const StreamInfo::StreamInfo&)>;

  DnsFormatterProvider(FieldExtractor field_extractor)
      : field_extractor_(std::move(field_extractor)) {}

  // FormatterProvider
  absl::optional<std::string> format(const Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override {
    return field_extractor_(context, stream_info);
  }

  Protobuf::Value formatValue(const Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override {
    const auto str = field_extractor_(context, stream_info);
    return str.has_value() ? ValueUtil::stringValue(str.value()) : ValueUtil::nullValue();
  }

private:
  const FieldExtractor field_extractor_;
};

/**
 * Helper to create formatter provider for query-dependent fields (fields that require queries_[0]).
 */
template <typename FieldAccessor>
Formatter::FormatterProviderPtr makeQueryFieldProvider(FieldAccessor accessor) {
  return std::make_unique<DnsFormatterProvider>(
      [accessor](const Formatter::Context& ctx,
                 const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
        const auto dns_ctx = ctx.typedExtension<DnsQueryContext>();
        if (!dns_ctx.has_value() || dns_ctx->queries_.empty()) {
          return absl::nullopt;
        }
        return absl::StrCat(accessor(*dns_ctx));
      });
}

/**
 * Helper to create formatter provider for context-level fields (fields that don't require queries).
 */
template <typename FieldAccessor>
Formatter::FormatterProviderPtr makeContextFieldProvider(FieldAccessor accessor) {
  return std::make_unique<DnsFormatterProvider>(
      [accessor](const Formatter::Context& ctx,
                 const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
        const auto dns_ctx = ctx.typedExtension<DnsQueryContext>();
        if (!dns_ctx.has_value()) {
          return absl::nullopt;
        }
        return accessor(*dns_ctx);
      });
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
               return makeQueryFieldProvider(
                   [](const DnsQueryContext& ctx) { return ctx.queries_[0]->name_; });
             }},
            {"QUERY_TYPE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeQueryFieldProvider(
                   [](const DnsQueryContext& ctx) { return ctx.queries_[0]->type_; });
             }},
            {"QUERY_CLASS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeQueryFieldProvider(
                   [](const DnsQueryContext& ctx) { return ctx.queries_[0]->class_; });
             }},
            {"ANSWER_COUNT",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeContextFieldProvider(
                   [](const DnsQueryContext& ctx) { return absl::StrCat(ctx.answers_.size()); });
             }},
            {"RESPONSE_CODE",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeContextFieldProvider(
                   [](const DnsQueryContext& ctx) { return absl::StrCat(ctx.response_code_); });
             }},
            {"PARSE_STATUS",
             [](absl::string_view, absl::optional<size_t>) -> Formatter::FormatterProviderPtr {
               return makeContextFieldProvider([](const DnsQueryContext& ctx) -> std::string {
                 return ctx.parse_status_ ? "true" : "false";
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
