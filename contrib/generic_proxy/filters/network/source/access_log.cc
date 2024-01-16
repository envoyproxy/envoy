#include "contrib/generic_proxy/filters/network/source/access_log.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class StringValueFormatterProvider : public FormatterProvider {
public:
  using ValueExtractor = std::function<absl::optional<std::string>(const FormatterContext&,
                                                                   const StreamInfo::StreamInfo&)>;

  StringValueFormatterProvider(ValueExtractor f, absl::optional<size_t> max_length = absl::nullopt)
      : value_extractor_(f), max_length_(max_length) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override {
    auto optional_str = value_extractor_(context, stream_info);
    if (!optional_str) {
      return absl::nullopt;
    }
    if (max_length_.has_value()) {
      if (optional_str->length() > max_length_.value()) {
        optional_str->resize(max_length_.value());
      }
    }
    return optional_str;
  }
  ProtobufWkt::Value
  formatValueWithContext(const FormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(formatWithContext(context, stream_info));
  }

private:
  ValueExtractor value_extractor_;
  absl::optional<size_t> max_length_;
};

class SimpleCommandParser : public CommandParser {
public:
  using ProviderFunc =
      std::function<FormatterProviderPtr(absl::string_view, absl::optional<size_t> max_length)>;
  using ProviderFuncTable = absl::flat_hash_map<std::string, ProviderFunc>;

  // CommandParser
  FormatterProviderPtr parse(const std::string& command, const std::string& command_arg,
                             absl::optional<size_t>& max_length) const override {
    const auto& provider_func_table = providerFuncTable();
    const auto func_iter = provider_func_table.find(std::string(command));
    if (func_iter == provider_func_table.end()) {
      return nullptr;
    }
    return func_iter->second(command_arg, max_length);
  }

private:
  static const ProviderFuncTable& providerFuncTable() {
    CONSTRUCT_ON_FIRST_USE(
        ProviderFuncTable,
        {
            {"METHOD",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->method());
                     }
                     return absl::nullopt;
                   });
             }},
            {"HOST",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->host());
                     }
                     return absl::nullopt;
                   });
             }},
            {"PATH",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->path());
                     }
                     return absl::nullopt;
                   });
             }},
            {"PROTOCOL",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (context.request_) {
                       return std::string(context.request_->protocol());
                     }
                     return absl::nullopt;
                   });
             }},
            {"REQUEST_PROPERTY",
             [](absl::string_view command_arg, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [key = std::string(command_arg)](
                       const FormatterContext& context,
                       const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (!context.request_) {
                       return absl::nullopt;
                     }
                     auto optional_view = context.request_->get(key);
                     if (!optional_view.has_value()) {
                       return absl::nullopt;
                     }
                     return std::string(optional_view.value());
                   });
             }},
            {"RESPONSE_PROPERTY",
             [](absl::string_view command_arg, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [key = std::string(command_arg)](
                       const FormatterContext& context,
                       const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     if (!context.response_) {
                       return absl::nullopt;
                     }
                     auto optional_view = context.response_->get(key);
                     if (!optional_view.has_value()) {
                       return absl::nullopt;
                     }
                     return std::string(optional_view.value());
                   });
             }},
        });
  }
};

// Register the access log for the FormatterContext.
REGISTER_FACTORY(FileAccessLogFactory, AccessLogInstanceFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions

namespace Formatter {

using FormatterContext = Extensions::NetworkFilters::GenericProxy::FormatterContext;
using SimpleCommandParser = Extensions::NetworkFilters::GenericProxy::SimpleCommandParser;
// Regiter the built-in command parsers for the FormatterContext.
REGISTER_BUILT_IN_COMMAND_PARSER(FormatterContext, SimpleCommandParser);

} // namespace Formatter

} // namespace Envoy
