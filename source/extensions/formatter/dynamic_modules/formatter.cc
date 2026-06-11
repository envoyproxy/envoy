#include "source/extensions/formatter/dynamic_modules/formatter.h"

#include <string>

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {

DynamicModuleFormatterConfig::DynamicModuleFormatterConfig(
    absl::string_view formatter_name, absl::string_view formatter_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : formatter_name_(formatter_name), formatter_config_(formatter_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleFormatterConfig::~DynamicModuleFormatterConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleFormatterConfigSharedPtr>
newDynamicModuleFormatterConfig(absl::string_view formatter_name,
                                absl::string_view formatter_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnFormatterConfigNewType>(
      "envoy_dynamic_module_on_formatter_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnFormatterConfigDestroyType>(
      "envoy_dynamic_module_on_formatter_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_parse = dynamic_module->getFunctionPointer<OnFormatterParseType>(
      "envoy_dynamic_module_on_formatter_parse");
  RETURN_IF_NOT_OK_REF(on_parse.status());

  auto on_provider_destroy = dynamic_module->getFunctionPointer<OnFormatterProviderDestroyType>(
      "envoy_dynamic_module_on_formatter_provider_destroy");
  RETURN_IF_NOT_OK_REF(on_provider_destroy.status());

  auto on_format = dynamic_module->getFunctionPointer<OnFormatterFormatType>(
      "envoy_dynamic_module_on_formatter_format");
  RETURN_IF_NOT_OK_REF(on_format.status());

  auto config = std::make_shared<DynamicModuleFormatterConfig>(formatter_name, formatter_config,
                                                               std::move(dynamic_module));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_parse_ = on_parse.value();
  config->on_provider_destroy_ = on_provider_destroy.value();
  config->on_format_ = on_format.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->formatter_name_.data(),
                                                     .length = config->formatter_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->formatter_config_.data(),
                                                       .length = config->formatter_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module formatter config");
  }
  return config;
}

DynamicModuleFormatterProvider::DynamicModuleFormatterProvider(
    DynamicModuleFormatterConfigSharedPtr config,
    envoy_dynamic_module_type_formatter_provider_module_ptr provider)
    : config_(std::move(config)), provider_(provider) {}

DynamicModuleFormatterProvider::~DynamicModuleFormatterProvider() {
  config_->on_provider_destroy_(provider_);
}

absl::optional<std::string>
DynamicModuleFormatterProvider::format(const ::Envoy::Formatter::Context& context,
                                       const StreamInfo::StreamInfo& stream_info) const {
  FormatterContext formatter_context{&context, &stream_info};
  envoy_dynamic_module_type_module_buffer result{nullptr, 0};
  if (!config_->on_format_(provider_, static_cast<void*>(&formatter_context), &result)) {
    return absl::nullopt;
  }
  if (result.length == 0) {
    return std::string();
  }
  return std::string(result.ptr, result.length);
}

Protobuf::Value
DynamicModuleFormatterProvider::formatValue(const ::Envoy::Formatter::Context& context,
                                            const StreamInfo::StreamInfo& stream_info) const {
  const absl::optional<std::string> value = format(context, stream_info);
  if (!value.has_value()) {
    return ValueUtil::nullValue();
  }
  return ValueUtil::stringValue(value.value());
}

DynamicModuleCommandParser::DynamicModuleCommandParser(DynamicModuleFormatterConfigSharedPtr config)
    : config_(std::move(config)) {}

::Envoy::Formatter::FormatterProviderPtr
DynamicModuleCommandParser::parse(absl::string_view command, absl::string_view command_arg,
                                  absl::optional<size_t> max_length) const {
  envoy_dynamic_module_type_envoy_buffer command_buf = {.ptr = command.data(),
                                                        .length = command.size()};
  envoy_dynamic_module_type_envoy_buffer command_arg_buf = {.ptr = command_arg.data(),
                                                            .length = command_arg.size()};
  auto provider = config_->on_parse_(config_->in_module_config_, command_buf, command_arg_buf,
                                     max_length.has_value(), max_length.value_or(0));
  if (provider == nullptr) {
    return nullptr;
  }
  return std::make_unique<DynamicModuleFormatterProvider>(config_, provider);
}

} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
