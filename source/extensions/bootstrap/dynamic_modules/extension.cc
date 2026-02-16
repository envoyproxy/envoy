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
  registerLifecycleCallbacks();
}

void DynamicModuleBootstrapExtension::onWorkerThreadInitialized() {
  if (in_module_extension_ == nullptr) {
    return;
  }
  config_->on_bootstrap_extension_worker_thread_initialized_(thisAsVoidPtr(), in_module_extension_);
}

void DynamicModuleBootstrapExtension::registerLifecycleCallbacks() {
  // Register for drain notifications via the server-wide drain manager.
  drain_handle_ = config_->context_.drainManager().addOnDrainCloseCb(
      Network::DrainDirection::All, [this](std::chrono::milliseconds) -> absl::Status {
        if (in_module_extension_ != nullptr) {
          ENVOY_LOG(debug, "dynamic module bootstrap extension drain started");
          config_->on_bootstrap_extension_drain_started_(thisAsVoidPtr(), in_module_extension_);
        }
        return absl::OkStatus();
      });

  // Register for shutdown notifications via the server lifecycle notifier.
  shutdown_handle_ = config_->context_.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::ShutdownExit, [this](Event::PostCb completion_cb) {
        if (in_module_extension_ != nullptr) {
          ENVOY_LOG(debug, "dynamic module bootstrap extension shutdown started");
          // Wrap the completion callback in a heap-allocated std::function so it can be passed
          // through the C ABI as an opaque context pointer.
          auto* completion = new Event::PostCb(std::move(completion_cb));
          config_->on_bootstrap_extension_shutdown_(
              thisAsVoidPtr(), in_module_extension_,
              [](void* context) {
                auto* cb = static_cast<Event::PostCb*>(context);
                (*cb)();
                delete cb;
              },
              static_cast<void*>(completion));
        } else {
          completion_cb();
        }
      });
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
