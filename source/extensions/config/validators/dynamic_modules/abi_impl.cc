// NOLINT(namespace-envoy)
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/extensions/config/validators/dynamic_modules/config_validator.h"

#include "absl/strings/string_view.h"

using ConfigValidatorContext =
    Envoy::Extensions::Config::Validators::DynamicModules::DynamicModuleConfigValidatorContext;

extern "C" {

void envoy_dynamic_module_callback_config_validator_set_rejection_message(
    envoy_dynamic_module_type_config_validator_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer rejection_message) {
  if (context_envoy_ptr == nullptr) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_config_validator_set_rejection_message null context");
    return;
  }
  if (rejection_message.ptr == nullptr && rejection_message.length != 0) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_config_validator_set_rejection_message null message "
        "with non-zero length");
    return;
  }
  auto* context = static_cast<ConfigValidatorContext*>(context_envoy_ptr);
  context->rejection_message =
      std::string(rejection_message.length == 0
                      ? absl::string_view()
                      : absl::string_view(rejection_message.ptr, rejection_message.length));
}

uint64_t envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count(
    envoy_dynamic_module_type_config_validator_context_envoy_ptr context_envoy_ptr) {
  if (context_envoy_ptr == nullptr) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count null context");
    return 0;
  }
  auto* context = static_cast<ConfigValidatorContext*>(context_envoy_ptr);
  return context->server.clusterManager().clusters().added_via_api_clusters_num_;
}

} // extern "C"
