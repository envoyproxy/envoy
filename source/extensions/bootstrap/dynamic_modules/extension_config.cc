#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

DynamicModuleBootstrapExtensionConfig::DynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher)
    : dynamic_module_(std::move(dynamic_module)), main_thread_dispatcher_(main_thread_dispatcher) {
  ASSERT(dynamic_module_ != nullptr);
  ASSERT(extension_name.data() != nullptr);
  ASSERT(extension_config.data() != nullptr);
}

DynamicModuleBootstrapExtensionConfig::~DynamicModuleBootstrapExtensionConfig() {
  if (in_module_config_ != nullptr && on_bootstrap_extension_config_destroy_ != nullptr) {
    on_bootstrap_extension_config_destroy_(in_module_config_);
  }
}

void DynamicModuleBootstrapExtensionConfig::onScheduled(uint64_t event_id) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_config_scheduled_ != nullptr) {
    on_bootstrap_extension_config_scheduled_(thisAsVoidPtr(), in_module_config_, event_id);
  }
}

absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher) {

  // Resolve the required symbols from the dynamic module.
  auto constructor =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_bootstrap_extension_config_new)>(
              "envoy_dynamic_module_on_bootstrap_extension_config_new");
  if (!constructor.ok()) {
    return constructor.status();
  }

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionConfigDestroyType>(
          "envoy_dynamic_module_on_bootstrap_extension_config_destroy");
  if (!on_config_destroy.ok()) {
    return on_config_destroy.status();
  }

  auto on_extension_new = dynamic_module->getFunctionPointer<OnBootstrapExtensionNewType>(
      "envoy_dynamic_module_on_bootstrap_extension_new");
  if (!on_extension_new.ok()) {
    return on_extension_new.status();
  }

  auto on_server_initialized =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionServerInitializedType>(
          "envoy_dynamic_module_on_bootstrap_extension_server_initialized");
  if (!on_server_initialized.ok()) {
    return on_server_initialized.status();
  }

  auto on_worker_thread_initialized =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionWorkerThreadInitializedType>(
          "envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized");
  if (!on_worker_thread_initialized.ok()) {
    return on_worker_thread_initialized.status();
  }

  auto on_extension_destroy = dynamic_module->getFunctionPointer<OnBootstrapExtensionDestroyType>(
      "envoy_dynamic_module_on_bootstrap_extension_destroy");
  if (!on_extension_destroy.ok()) {
    return on_extension_destroy.status();
  }

  auto on_config_scheduled =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionConfigScheduledType>(
          "envoy_dynamic_module_on_bootstrap_extension_config_scheduled");
  if (!on_config_scheduled.ok()) {
    return on_config_scheduled.status();
  }

  auto config = std::make_shared<DynamicModuleBootstrapExtensionConfig>(
      extension_name, extension_config, std::move(dynamic_module), main_thread_dispatcher);

  const void* extension_config_module_ptr = (*constructor.value())(
      static_cast<void*>(config.get()), {extension_name.data(), extension_name.size()},
      {extension_config.data(), extension_config.size()});
  if (extension_config_module_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module");
  }

  config->in_module_config_ = extension_config_module_ptr;
  config->on_bootstrap_extension_config_destroy_ = on_config_destroy.value();
  config->on_bootstrap_extension_new_ = on_extension_new.value();
  config->on_bootstrap_extension_server_initialized_ = on_server_initialized.value();
  config->on_bootstrap_extension_worker_thread_initialized_ = on_worker_thread_initialized.value();
  config->on_bootstrap_extension_destroy_ = on_extension_destroy.value();
  config->on_bootstrap_extension_config_scheduled_ = on_config_scheduled.value();

  return config;
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
