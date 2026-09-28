#include "source/extensions/config/validators/dynamic_modules/config_validator.h"

#include <optional>

#include "envoy/common/exception.h"

#include "source/common/common/thread.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {
namespace {

envoy_dynamic_module_type_envoy_buffer makeEnvoyBuffer(absl::string_view value) {
  return {value.data(), value.size()};
}

// Backing storage for buffers that Envoy computes and hands across the ABI. It must outlive the
// validation call. The name, version, and alias strings are borrowed from the live DecodedResource,
// so they need no backing here.
struct SerializedResource {
  std::string serialized_resource;
  std::vector<envoy_dynamic_module_type_envoy_buffer> aliases;
};

std::vector<envoy_dynamic_module_type_config_validator_resource>
serializeResources(const std::vector<Envoy::Config::DecodedResourcePtr>& resources,
                   std::vector<SerializedResource>& serialized_resources) {
  serialized_resources.resize(resources.size());
  std::vector<envoy_dynamic_module_type_config_validator_resource> abi_resources(resources.size());
  for (size_t i = 0; i < resources.size(); ++i) {
    const auto& resource = resources[i];
    SerializedResource& serialized = serialized_resources[i];

    const std::vector<std::string>& aliases = resource->aliases();
    serialized.aliases.reserve(aliases.size());
    for (const auto& alias : aliases) {
      serialized.aliases.push_back(makeEnvoyBuffer(alias));
    }

    const std::optional<std::chrono::milliseconds> ttl = resource->ttl();
    const bool has_resource = resource->hasResource();
    if (has_resource && !resource->resource().SerializeToString(&serialized.serialized_resource)) {
      throwEnvoyExceptionOrPanic(
          absl::StrCat("Failed to serialize xDS resource for dynamic module config validator ",
                       resource->name()));
    }

    abi_resources[i] = {
        makeEnvoyBuffer(resource->name()),
        makeEnvoyBuffer(resource->version()),
        serialized.aliases.empty() ? nullptr : serialized.aliases.data(),
        serialized.aliases.size(),
        ttl.has_value(),
        ttl.has_value() ? static_cast<uint64_t>(ttl->count()) : 0,
        has_resource,
        has_resource ? makeEnvoyBuffer(serialized.serialized_resource)
                     : envoy_dynamic_module_type_envoy_buffer{nullptr, 0},
    };
  }
  return abi_resources;
}

std::vector<envoy_dynamic_module_type_envoy_buffer>
removedResourceBuffers(const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  std::vector<envoy_dynamic_module_type_envoy_buffer> buffers;
  buffers.reserve(removed_resources.size());
  for (const auto& removed_resource : removed_resources) {
    buffers.push_back(makeEnvoyBuffer(removed_resource));
  }
  return buffers;
}

void throwIfRejected(const DynamicModuleConfigValidatorContext& context, absl::string_view type_url,
                     bool accepted) {
  if (accepted) {
    return;
  }
  if (context.rejection_message.has_value() && !context.rejection_message->empty()) {
    throwEnvoyExceptionOrPanic(absl::StrCat("dynamic module config validator rejected ", type_url,
                                            " update, ", context.rejection_message.value()));
  }
  throwEnvoyExceptionOrPanic(
      absl::StrCat("dynamic module config validator rejected ", type_url, " update"));
}

} // namespace

DynamicModuleConfigValidatorConfig::DynamicModuleConfigValidatorConfig(
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleConfigValidatorConfig::~DynamicModuleConfigValidatorConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleConfigValidatorConfigSharedPtr> newDynamicModuleConfigValidatorConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnConfigValidatorConfigNewType>(
      "envoy_dynamic_module_on_config_validator_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnConfigValidatorConfigDestroyType>(
      "envoy_dynamic_module_on_config_validator_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_validate = dynamic_module->getFunctionPointer<OnConfigValidatorValidateType>(
      "envoy_dynamic_module_on_config_validator_validate");
  RETURN_IF_NOT_OK_REF(on_validate.status());

  auto on_validate_delta = dynamic_module->getFunctionPointer<OnConfigValidatorValidateDeltaType>(
      "envoy_dynamic_module_on_config_validator_validate_delta");
  RETURN_IF_NOT_OK_REF(on_validate_delta.status());

  auto config = std::make_shared<DynamicModuleConfigValidatorConfig>(std::move(dynamic_module));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_validate_ = on_validate.value();
  config->on_validate_delta_ = on_validate_delta.value();

  // These buffers borrow from the caller's strings and are valid only for the config_new call.
  const envoy_dynamic_module_type_envoy_buffer name_buffer = makeEnvoyBuffer(extension_name);
  const envoy_dynamic_module_type_envoy_buffer config_buffer = makeEnvoyBuffer(extension_config);
  config->in_module_config_ =
      on_config_new.value()(static_cast<void*>(config.get()), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module config validator");
  }
  return config;
}

DynamicModuleConfigValidator::DynamicModuleConfigValidator(
    absl::string_view type_url, DynamicModuleConfigValidatorConfigSharedPtr config)
    : type_url_(type_url), config_(std::move(config)) {}

void DynamicModuleConfigValidator::validate(
    const Server::Instance& server,
    const std::vector<Envoy::Config::DecodedResourcePtr>& resources) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  DynamicModuleConfigValidatorContext context{server, std::nullopt};
  std::vector<SerializedResource> serialized_resources;
  const std::vector<envoy_dynamic_module_type_config_validator_resource> abi_resources =
      serializeResources(resources, serialized_resources);
  const bool accepted =
      config_->on_validate_(static_cast<void*>(&context), config_->in_module_config_,
                            makeEnvoyBuffer(type_url_), abi_resources.data(), abi_resources.size());
  throwIfRejected(context, type_url_, accepted);
}

void DynamicModuleConfigValidator::validate(
    const Server::Instance& server,
    const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  DynamicModuleConfigValidatorContext context{server, std::nullopt};
  std::vector<SerializedResource> serialized_resources;
  const std::vector<envoy_dynamic_module_type_config_validator_resource> abi_added_resources =
      serializeResources(added_resources, serialized_resources);
  const std::vector<envoy_dynamic_module_type_envoy_buffer> abi_removed_resources =
      removedResourceBuffers(removed_resources);
  const bool accepted = config_->on_validate_delta_(
      static_cast<void*>(&context), config_->in_module_config_, makeEnvoyBuffer(type_url_),
      abi_added_resources.data(), abi_added_resources.size(), abi_removed_resources.data(),
      abi_removed_resources.size());
  throwIfRejected(context, type_url_, accepted);
}

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
