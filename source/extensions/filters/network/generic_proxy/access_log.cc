#include "source/extensions/filters/network/generic_proxy/access_log.h"

#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

absl::optional<std::string>
StringValueFormatterProvider::formatWithContext(const FormatterContext& context,
                                                const StreamInfo::StreamInfo& stream_info) const {
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
ProtobufWkt::Value StringValueFormatterProvider::formatValueWithContext(
    const FormatterContext& context, const StreamInfo::StreamInfo& stream_info) const {
  return ValueUtil::optionalStringValue(formatWithContext(context, stream_info));
}

absl::optional<std::string>
GenericStatusCodeFormatterProvider::formatWithContext(const FormatterContext& context,
                                                      const StreamInfo::StreamInfo&) const {
  CHECK_DATA_OR_RETURN(context, response_, absl::nullopt);
  const int code = checked_data->response_->status().code();
  return std::to_string(code);
}

ProtobufWkt::Value
GenericStatusCodeFormatterProvider::formatValueWithContext(const FormatterContext& context,
                                                           const StreamInfo::StreamInfo&) const {
  CHECK_DATA_OR_RETURN(context, response_, ValueUtil::nullValue());
  const int code = checked_data->response_->status().code();
  return ValueUtil::numberValue(code);
}

class GenericProxyCommandParser : public Formatter::CommandParser {
public:
  using ProviderFunc =
      std::function<FormatterProviderPtr(absl::string_view, absl::optional<size_t> max_length)>;
  using ProviderFuncTable = absl::flat_hash_map<std::string, ProviderFunc>;

  // CommandParser
  FormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                             absl::optional<size_t> max_length) const override {
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
                     CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);
                     return std::string(checked_data->request_->method());
                   });
             }},
            {"HOST",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);
                     return std::string(checked_data->request_->host());
                   });
             }},
            {"PATH",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);
                     return std::string(checked_data->request_->path());
                   });
             }},
            {"PROTOCOL",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context,
                      const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);
                     return std::string(checked_data->request_->protocol());
                   });
             }},
            {"REQUEST_PROPERTY",
             [](absl::string_view command_arg, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [key = std::string(command_arg)](
                       const FormatterContext& context,
                       const StreamInfo::StreamInfo&) -> absl::optional<std::string> {
                     CHECK_DATA_OR_RETURN(context, request_, absl::nullopt);

                     auto optional_view = checked_data->request_->get(key);
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
                     CHECK_DATA_OR_RETURN(context, response_, absl::nullopt);
                     auto optional_view = checked_data->response_->get(key);
                     if (!optional_view.has_value()) {
                       return absl::nullopt;
                     }
                     return std::string(optional_view.value());
                   });
             }},
            {"GENERIC_RESPONSE_CODE",
             [](absl::string_view, absl::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<GenericStatusCodeFormatterProvider>();
             }},
        });
  }
};

Formatter::CommandParserPtr createGenericProxyCommandParser() {
  return std::make_unique<GenericProxyCommandParser>();
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
