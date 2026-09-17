#include "source/extensions/filters/network/generic_proxy/access_log.h"

#include <string>

#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

std::optional<std::string>
StringValueFormatterProvider::format(const FormatterContext& context,
                                     const StreamInfo::StreamInfo&) const {
  if (auto result = truncate(value_extractor_(context)); result.has_value()) {
    return std::string(*result);
  }
  return std::nullopt;
}
Protobuf::Value
StringValueFormatterProvider::formatValue(const FormatterContext& context,
                                          const StreamInfo::StreamInfo& stream_info) const {
  return ValueUtil::optionalStringValue(format(context, stream_info));
}

bool StringValueFormatterProvider::formatTo(std::string& sink, const FormatterContext& context,
                                            const StreamInfo::StreamInfo&) const {
  if (auto result = truncate(value_extractor_(context)); result.has_value()) {
    sink.append(*result);
    return true;
  }
  return false;
}

void StringValueFormatterProvider::formatValueTo(Formatter::ValueSink& sink,
                                                 const FormatterContext& context,
                                                 const StreamInfo::StreamInfo&) const {
  if (auto result = truncate(value_extractor_(context)); result.has_value()) {
    sink.addString(*result);
  }
}

std::optional<absl::string_view>
StringValueFormatterProvider::truncate(std::optional<absl::string_view> optional_str) const {
  if (!optional_str.has_value()) {
    return std::nullopt;
  }
  // If no max length limit or the string is shorter than the limit, return the original string.
  if (!max_length_.has_value() || optional_str->size() <= max_length_.value()) {
    return optional_str;
  }
  return optional_str->substr(0, max_length_.value());
}

std::optional<std::string>
GenericStatusCodeFormatterProvider::format(const FormatterContext& context,
                                           const StreamInfo::StreamInfo&) const {
  CHECK_DATA_OR_RETURN(context, response_, std::nullopt);
  const int code = checked_data->response_->status().code();
  return std::to_string(code);
}

Protobuf::Value
GenericStatusCodeFormatterProvider::formatValue(const FormatterContext& context,
                                                const StreamInfo::StreamInfo&) const {
  CHECK_DATA_OR_RETURN(context, response_, ValueUtil::nullValue());
  const int code = checked_data->response_->status().code();
  return ValueUtil::numberValue(code);
}

bool GenericStatusCodeFormatterProvider::formatTo(std::string& sink,
                                                  const FormatterContext& context,
                                                  const StreamInfo::StreamInfo&) const {
  CHECK_DATA_OR_RETURN(context, response_, false);
  absl::StrAppend(&sink, checked_data->response_->status().code());
  return true;
}

void GenericStatusCodeFormatterProvider::formatValueTo(Formatter::ValueSink& sink,
                                                       const FormatterContext& context,
                                                       const StreamInfo::StreamInfo&) const {
  // Keep the sink unmodified if there is no response so the caller can decide how to handle it.
  CHECK_DATA_OR_RETURN(context, response_, );
  sink.addNumber(static_cast<int64_t>(checked_data->response_->status().code()));
}

class GenericProxyCommandParser : public Formatter::CommandParser {
public:
  using ProviderFunc =
      std::function<FormatterProviderPtr(absl::string_view, std::optional<size_t> max_length)>;
  using ProviderFuncTable = absl::flat_hash_map<std::string, ProviderFunc>;

  // CommandParser
  absl::StatusOr<Formatter::FormatterProviderPtr>
  parse(absl::string_view command, absl::string_view command_arg,
        std::optional<size_t> max_length) const override {
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
             [](absl::string_view, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, request_, std::nullopt);
                     return checked_data->request_->method();
                   });
             }},
            {"HOST",
             [](absl::string_view, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, request_, std::nullopt);
                     return checked_data->request_->host();
                   });
             }},
            {"PATH",
             [](absl::string_view, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, request_, std::nullopt);
                     return checked_data->request_->path();
                   });
             }},
            {"PROTOCOL",
             [](absl::string_view, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [](const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, request_, std::nullopt);
                     return checked_data->request_->protocol();
                   });
             }},
            {"REQUEST_PROPERTY",
             [](absl::string_view command_arg, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [key = std::string(command_arg)](
                       const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, request_, std::nullopt);
                     return checked_data->request_->get(key);
                   });
             }},
            {"RESPONSE_PROPERTY",
             [](absl::string_view command_arg, std::optional<size_t>) -> FormatterProviderPtr {
               return std::make_unique<StringValueFormatterProvider>(
                   [key = std::string(command_arg)](
                       const FormatterContext& context) -> std::optional<absl::string_view> {
                     CHECK_DATA_OR_RETURN(context, response_, std::nullopt);
                     return checked_data->response_->get(key);
                   });
             }},
            {"GENERIC_RESPONSE_CODE",
             [](absl::string_view, std::optional<size_t>) -> FormatterProviderPtr {
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
