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

class GenericProxyCommandParserFactory : public Formatter::CommandParserFactory {
public:
  // CommandParserFactory
  Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message&,
                               Server::Configuration::GenericFactoryContext&) override {
    return createGenericProxyCommandParser();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::network::generic_proxy::v3::
                                GenericProxySubstitutionFormatter>();
  }

  std::string name() const override { return "envoy.formatters.generic_proxy_commands"; }
};

REGISTER_FACTORY(GenericProxyCommandParserFactory, Formatter::CommandParserFactory);

absl::Status initializeFormatterForFileLogger(
    envoy::extensions::access_loggers::file::v3::FileAccessLog& config) {
  switch (config.access_log_format_case()) {
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kFormat: {
    std::string legacy_format = config.format();
    *config.mutable_log_format()->mutable_text_format_source()->mutable_inline_string() =
        legacy_format;
    break;
  }
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      kJsonFormat: {
    ProtobufWkt::Struct json_format = config.json_format();
    *config.mutable_log_format()->mutable_json_format() = std::move(json_format);
    break;
  }
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      kTypedJsonFormat: {
    ProtobufWkt::Struct json_format = config.typed_json_format();
    *config.mutable_log_format()->mutable_json_format() = std::move(json_format);
    break;
  }
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kLogFormat:
    break;
  case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
      ACCESS_LOG_FORMAT_NOT_SET:
    return absl::InvalidArgumentError(
        "Access log: no format and no default format for file access log");
  }

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxySubstitutionFormatter
      formatter;
  auto* formatter_extension = config.mutable_log_format()->mutable_formatters()->Add();
  formatter_extension->set_name("envoy.formatters.generic_proxy_commands");
  formatter_extension->mutable_typed_config()->PackFrom(formatter);
  return absl::OkStatus();
}

AccessLog::FilterPtr
accessLogFilterFromProto(const envoy::config::accesslog::v3::AccessLogFilter& config,
                         Server::Configuration::FactoryContext& context) {
  if (!config.has_extension_filter()) {
    ExceptionUtil::throwEnvoyException(
        "Access log filter: only extension filter is supported by non-HTTP access loggers.");
  }

  auto& factory = Config::Utility::getAndCheckFactory<Envoy::AccessLog::ExtensionFilterFactory>(
      config.extension_filter());
  return factory.createFilter(config.extension_filter(), context);
}

AccessLog::InstanceSharedPtr
accessLoggerFromProto(const envoy::config::accesslog::v3::AccessLog& config,
                      Server::Configuration::FactoryContext& context) {
  AccessLog::FilterPtr filter;
  if (config.has_filter()) {
    filter = accessLogFilterFromProto(config.filter(), context);
  }

  auto& factory =
      Config::Utility::getAndCheckFactory<Envoy::AccessLog::AccessLogInstanceFactory>(config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);

  // Setup the generic proxy formatter for file access loggers by default. This is necessary
  // for backwards compatibility.
  if (factory.name() == "envoy.access_loggers.file") {
    auto* file_config =
        dynamic_cast<envoy::extensions::access_loggers::file::v3::FileAccessLog*>(message.get());
    ASSERT(file_config != nullptr);
    THROW_IF_NOT_OK(initializeFormatterForFileLogger(*file_config));
  }

  // Inject the command parsers from the generic proxy.
  return factory.createAccessLogInstance(*message, std::move(filter), context);
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
