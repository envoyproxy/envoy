#include "source/extensions/bootstrap/dynamic_modules/extension.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

DynamicModuleBootstrapExtension::DynamicModuleBootstrapExtension(
    DynamicModuleBootstrapExtensionConfigSharedPtr config)
    : config_(config) {}

DynamicModuleBootstrapExtension::~DynamicModuleBootstrapExtension() {
  // The server can go away without a shutdown, for example in init only mode.
  config_->onExtensionDestroyed();
  destroy();
}

bool DynamicModuleBootstrapExtension::initializeInModuleExtension() {
  in_module_extension_ =
      config_->on_bootstrap_extension_new_(config_->in_module_config_, thisAsVoidPtr());
  return in_module_extension_ != nullptr;
}

void DynamicModuleBootstrapExtension::destroy() {
  if (in_module_extension_ != nullptr) {
    config_->on_bootstrap_extension_destroy_(in_module_extension_);
    in_module_extension_ = nullptr;
  }
  destroyed_ = true;
}

void DynamicModuleBootstrapExtension::onServerInitialized(Server::Instance& server) {
  if (in_module_extension_ == nullptr) {
    return;
  }
  config_->setListenerManager(server.listenerManager());
  // A server that fails to start can shut down before it initializes its extensions.
  if (server.isShutdown()) {
    config_->onShutdown();
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

  // Callbacks with a completion only run once the workers started. The configuration can outlive
  // the server's callback lists, so this extension holds the registration.
  config_shutdown_handle_ = config_->context_.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::ShutdownExit, [this]() { config_->onShutdown(); });
  post_init_handle_ = config_->context_.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::PostInit, [this]() { config_->onPostInit(); });
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
