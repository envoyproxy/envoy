#include "source/extensions/config/validators/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/config/validators/dynamic_modules/config_validator.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {
namespace {

using DynamicModuleConfigValidatorProto =
    envoy::extensions::config::validators::dynamic_modules::v3::DynamicModuleConfigValidator;

std::string extensionConfigBytesOrThrow(const DynamicModuleConfigValidatorProto& proto_config) {
  if (!proto_config.has_extension_config()) {
    return "";
  }
  auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.extension_config());
  if (!config_or_error.ok()) {
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to parse dynamic module config validator extension_config, ",
                     config_or_error.status().message()));
  }
  return std::move(config_or_error.value());
}

std::string typeUrlOrThrow(const DynamicModuleConfigValidatorProto& proto_config) {
  const std::string& type_url = proto_config.type_url();
  const absl::string_view descriptor_full_name = TypeUtil::typeUrlToDescriptorFullName(type_url);
  if (descriptor_full_name.empty() ||
      TypeUtil::descriptorFullNameToTypeUrl(descriptor_full_name) != type_url) {
    throwEnvoyExceptionOrPanic("dynamic module config validator type_url must use the canonical "
                               "type.googleapis.com/<message> form");
  }
  return type_url;
}

} // namespace

Envoy::Config::ConfigValidatorPtr DynamicModuleConfigValidatorFactory::createConfigValidator(
    const Protobuf::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& proto_config = MessageUtil::anyConvertAndValidate<DynamicModuleConfigValidatorProto>(
      config, validation_visitor);
  const std::string type_url = typeUrlOrThrow(proto_config);

  // Config validators have no factory context, so only the synchronous local-file and by-name
  // module sources can succeed here. A remote source is rejected by the loader.
  auto load_result = Envoy::Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.extension_name());
  if (!load_result.ok()) {
    throwEnvoyExceptionOrPanic(std::string(load_result.status().message()));
  }
  ASSERT(load_result->loaded != nullptr);

  const std::string extension_config = extensionConfigBytesOrThrow(proto_config);
  auto validator_config = newDynamicModuleConfigValidatorConfig(
      proto_config.extension_name(), extension_config, std::move(load_result->loaded));
  if (!validator_config.ok()) {
    throwEnvoyExceptionOrPanic(absl::StrCat("Failed to create dynamic module config validator, ",
                                            validator_config.status().message()));
  }
  return std::make_unique<DynamicModuleConfigValidator>(type_url,
                                                        std::move(validator_config.value()));
}

Envoy::ProtobufTypes::MessagePtr DynamicModuleConfigValidatorFactory::createEmptyConfigProto() {
  return std::make_unique<DynamicModuleConfigValidatorProto>();
}

std::string DynamicModuleConfigValidatorFactory::typeUrl() const {
  IS_ENVOY_BUG("dynamic module config validator type URL requires typed configuration");
  return "";
}

std::string DynamicModuleConfigValidatorFactory::typeUrlFromConfig(
    const Protobuf::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor) const {
  const auto& proto_config = MessageUtil::anyConvertAndValidate<DynamicModuleConfigValidatorProto>(
      config, validation_visitor);
  return typeUrlOrThrow(proto_config);
}

REGISTER_FACTORY(DynamicModuleConfigValidatorFactory, Envoy::Config::ConfigValidatorFactory);

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
