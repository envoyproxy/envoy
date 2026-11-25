#include "source/extensions/bootstrap/dynamic_modules/extension.h"

#include <cmath>
#include <vector>

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
  in_server_initialized_ = true;
  config_->on_bootstrap_extension_server_initialized_(thisAsVoidPtr(), in_module_extension_);
  in_server_initialized_ = false;
  registerLifecycleCallbacks();
}

void DynamicModuleBootstrapExtension::onWorkerThreadInitialized() {
  if (in_module_extension_ == nullptr) {
    return;
  }
  config_->on_bootstrap_extension_worker_thread_initialized_(thisAsVoidPtr(), in_module_extension_);
}

bool DynamicModuleBootstrapExtension::enableRuntimeUpdate(absl::string_view key) {
  if (config_->on_bootstrap_extension_runtime_bool_update_ == nullptr &&
      config_->on_bootstrap_extension_runtime_int_update_ == nullptr &&
      config_->on_bootstrap_extension_runtime_number_update_ == nullptr &&
      config_->on_bootstrap_extension_runtime_string_update_ == nullptr) {
    ENVOY_LOG(error, "cannot enable runtime update since the module implements none of the "
                     "envoy_dynamic_module_on_bootstrap_extension_runtime_*_update hooks");
    return false;
  }
  if (!in_server_initialized_) {
    ENVOY_LOG(error, "cannot enable runtime update outside of "
                     "envoy_dynamic_module_on_bootstrap_extension_server_initialized");
    return false;
  }
  if (config_->shutdownStarted()) {
    ENVOY_LOG(error, "cannot enable runtime update once shutdown started");
    return false;
  }
  if (key.empty()) {
    ENVOY_LOG(error, "cannot enable runtime update for an empty key");
    return false;
  }
  if (watched_runtime_values_.contains(key)) {
    return false;
  }
  Runtime::Loader& runtime = config_->context_.runtime();
  if (runtime_update_handle_ == nullptr) {
    runtime_update_handle_ = runtime.addUpdateCallback([this]() { onRuntimeUpdate(); });
  }
  watched_runtime_values_.emplace(key, runtimeValue(*runtime.threadsafeSnapshot(), key));
  return true;
}

std::optional<DynamicModuleBootstrapExtension::RuntimeValue>
DynamicModuleBootstrapExtension::runtimeValue(const Runtime::Snapshot& snapshot,
                                              absl::string_view key) {
  // Snapshot::get() asserts against runtime feature keys, so the layers are searched instead.
  const std::vector<Runtime::Snapshot::OverrideLayerConstPtr>& layers = snapshot.getLayers();
  for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
    const auto entry = (*layer)->values().find(key);
    if (entry == (*layer)->values().end()) {
      continue;
    }
    const Runtime::Snapshot::Entry& value = entry->second;
    // An integral number also has a boolean value, so the numbers are checked first.
    if (value.double_value_.has_value()) {
      if (value.uint_value_.has_value() &&
          static_cast<double>(*value.uint_value_) == *value.double_value_) {
        return RuntimeValue(absl::in_place_type<uint64_t>, *value.uint_value_);
      }
      return RuntimeValue(absl::in_place_type<double>, *value.double_value_);
    }
    if (value.bool_value_.has_value()) {
      return RuntimeValue(absl::in_place_type<bool>, *value.bool_value_);
    }
    // A fractional percent is a structured value, which the string hook does not report.
    if (!value.fractional_percent_value_.has_value() && !value.raw_string_value_.empty()) {
      return RuntimeValue(absl::in_place_type<std::string>, value.raw_string_value_);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool DynamicModuleBootstrapExtension::sameRuntimeValue(const std::optional<RuntimeValue>& a,
                                                       const std::optional<RuntimeValue>& b) {
  if (a.has_value() && b.has_value() && absl::holds_alternative<double>(*a) &&
      absl::holds_alternative<double>(*b) && std::isnan(absl::get<double>(*a)) &&
      std::isnan(absl::get<double>(*b))) {
    return true;
  }
  return a == b;
}

void DynamicModuleBootstrapExtension::reportRuntimeUpdate(absl::string_view key,
                                                          const RuntimeValue& value, bool removed) {
  const envoy_dynamic_module_type_envoy_buffer key_buffer = {key.data(), key.size()};
  if (absl::holds_alternative<bool>(value)) {
    if (config_->on_bootstrap_extension_runtime_bool_update_ != nullptr) {
      config_->on_bootstrap_extension_runtime_bool_update_(
          thisAsVoidPtr(), in_module_extension_, key_buffer, absl::get<bool>(value), removed);
    }
  } else if (absl::holds_alternative<uint64_t>(value)) {
    if (config_->on_bootstrap_extension_runtime_int_update_ != nullptr) {
      config_->on_bootstrap_extension_runtime_int_update_(
          thisAsVoidPtr(), in_module_extension_, key_buffer, absl::get<uint64_t>(value), removed);
    }
  } else if (absl::holds_alternative<double>(value)) {
    if (config_->on_bootstrap_extension_runtime_number_update_ != nullptr) {
      config_->on_bootstrap_extension_runtime_number_update_(
          thisAsVoidPtr(), in_module_extension_, key_buffer, absl::get<double>(value), removed);
    }
  } else if (config_->on_bootstrap_extension_runtime_string_update_ != nullptr) {
    const std::string& text = absl::get<std::string>(value);
    config_->on_bootstrap_extension_runtime_string_update_(
        thisAsVoidPtr(), in_module_extension_, key_buffer, {text.data(), text.size()}, removed);
  }
}

void DynamicModuleBootstrapExtension::onRuntimeUpdate() {
  if (config_->shutdownStarted()) {
    return;
  }
  const Runtime::SnapshotConstSharedPtr snapshot = config_->context_.runtime().threadsafeSnapshot();
  for (auto& [key, value] : watched_runtime_values_) {
    std::optional<RuntimeValue> new_value = runtimeValue(*snapshot, key);
    if (sameRuntimeValue(new_value, value)) {
      continue;
    }
    if (new_value.has_value()) {
      value = new_value;
      reportRuntimeUpdate(key, *value, false);
    } else {
      const RuntimeValue previous_value = *value;
      value.reset();
      reportRuntimeUpdate(key, previous_value, true);
    }
  }
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
