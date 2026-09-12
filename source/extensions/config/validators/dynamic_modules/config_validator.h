#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/config_validator.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {

using OnConfigValidatorConfigNewType =
    decltype(&envoy_dynamic_module_on_config_validator_config_new);
using OnConfigValidatorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_config_validator_config_destroy);
using OnConfigValidatorValidateType = decltype(&envoy_dynamic_module_on_config_validator_validate);
using OnConfigValidatorValidateDeltaType =
    decltype(&envoy_dynamic_module_on_config_validator_validate_delta);

// Per-call state for a single validation. The module receives a pointer to this context and uses it
// to read current server state and to set a rejection reason. It is valid only for the duration of
// the validate call.
struct DynamicModuleConfigValidatorContext {
  const Server::Instance& server;
  std::optional<std::string> rejection_message;
};

// Holds the resolved dynamic module and ABI function pointers for a config validator. The validator
// owns it and uses it for each validation it runs.
class DynamicModuleConfigValidatorConfig {
public:
  explicit DynamicModuleConfigValidatorConfig(
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);
  ~DynamicModuleConfigValidatorConfig();

  // Resolved during creation and treated as immutable afterwards.
  envoy_dynamic_module_type_config_validator_config_module_ptr in_module_config_{nullptr};
  OnConfigValidatorConfigDestroyType on_config_destroy_{nullptr};
  OnConfigValidatorValidateType on_validate_{nullptr};
  OnConfigValidatorValidateDeltaType on_validate_delta_{nullptr};

private:
  // Keep the module owned by the config so its destroy callback is callable before the shared
  // object is closed.
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleConfigValidatorConfigSharedPtr =
    std::shared_ptr<DynamicModuleConfigValidatorConfig>;

// Resolves the ABI symbols and initializes the in-module config. Returns an error when a symbol is
// missing or the module fails to initialize.
absl::StatusOr<DynamicModuleConfigValidatorConfigSharedPtr> newDynamicModuleConfigValidatorConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

// ConfigValidator implementation backed by a dynamic module.
class DynamicModuleConfigValidator : public Envoy::Config::ConfigValidator {
public:
  DynamicModuleConfigValidator(absl::string_view type_url,
                               DynamicModuleConfigValidatorConfigSharedPtr config);

  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& resources) override;

  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
                const Protobuf::RepeatedPtrField<std::string>& removed_resources) override;

private:
  const std::string type_url_;
  DynamicModuleConfigValidatorConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
