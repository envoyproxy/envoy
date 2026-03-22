#include "source/extensions/bootstrap/dynamic_modules/extension.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

DynamicModuleBootstrapExtension::DynamicModuleBootstrapExtension(
    DynamicModuleBootstrapExtensionConfigSharedPtr config)
    : config_(config) {}

DynamicModuleBootstrapExtension::~DynamicModuleBootstrapExtension() { destroy(); }

void DynamicModuleBootstrapExtension::initializeInModuleExtension() {
  in_module_extension_ =
      config_->on_bootstrap_extension_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleBootstrapExtension::destroy() {
  if (in_module_extension_ != nullptr) {
    config_->on_bootstrap_extension_destroy_(in_module_extension_);
    in_module_extension_ = nullptr;
  }
  destroyed_ = true;
}

void DynamicModuleBootstrapExtension::onServerInitialized() {
  if (in_module_extension_ == nullptr) {
    return;
  }
  config_->on_bootstrap_extension_server_initialized_(thisAsVoidPtr(), in_module_extension_);
}

void DynamicModuleBootstrapExtension::onWorkerThreadInitialized() {
  if (in_module_extension_ == nullptr) {
    return;
  }
  config_->on_bootstrap_extension_worker_thread_initialized_(thisAsVoidPtr(), in_module_extension_);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
