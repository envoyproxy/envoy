#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

DynamicModuleHttpFilterConfig::DynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : filter_name_(filter_name), filter_config_(filter_config),
      dynamic_module_(std::move(dynamic_module)){};

DynamicModuleHttpFilterConfig::~DynamicModuleHttpFilterConfig() {
  (*in_module_config_destroy_)(in_module_config_);
};

absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr>
newDynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                 const absl::string_view filter_config,
                                 Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  auto constructor =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_config_new)>(
          "envoy_dynamic_module_on_http_filter_config_new");
  if (constructor == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to resolve symbol envoy_dynamic_module_on_http_filter_config_new");
  }

  auto destroy =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_config_destroy)>(
              "envoy_dynamic_module_on_http_filter_config_destroy");
  if (destroy == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to resolve symbol envoy_dynamic_module_on_http_filter_config_destroy");
  }

  auto config = std::make_shared<DynamicModuleHttpFilterConfig>(filter_name, filter_config,
                                                                std::move(dynamic_module));

  const void* filter_config_envoy_ptr =
      (*constructor)(static_cast<void*>(config.get()), filter_name.data(), filter_name.size(),
                     filter_config.data(), filter_config.size());

  if (filter_config_envoy_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module");
  }

  config->in_module_config_ = filter_config_envoy_ptr;
  config->in_module_config_destroy_ = destroy;
  return config;
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
