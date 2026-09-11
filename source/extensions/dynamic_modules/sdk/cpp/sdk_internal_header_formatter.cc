#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk_header_formatter.h"

namespace Envoy {
namespace DynamicModules {

namespace {

template <class T> T* unwrapPointer(const void* ptr) {
  return const_cast<T*>(static_cast<const T*>(ptr));
}

template <class T> void* wrapPointer(const T* ptr) {
  return reinterpret_cast<void*>(const_cast<T*>(ptr));
}

class HeaderFormatterConfigHandleImpl : public HeaderFormatterConfigHandle {
public:
  // Header formatting exposes no config-scoped callbacks, so the Envoy config pointer is not
  // retained.
  explicit HeaderFormatterConfigHandleImpl(
      envoy_dynamic_module_type_header_formatter_config_envoy_ptr) {}

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }
};

// Per-message handle. It is created with the formatter and owned by HeaderFormatterWrapper, so it
// stays valid for as long as the formatter it was handed to.
class HeaderFormatterHandleImpl : public HeaderFormatterHandle {
public:
  explicit HeaderFormatterHandleImpl(envoy_dynamic_module_type_header_formatter_envoy_ptr) {}

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  LogLevel getLogLevel() override {
    return static_cast<LogLevel>(envoy_dynamic_module_callback_get_log_level());
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }
};

// The in-module configuration. One is created per configured entry and is shared by every message
// on every worker thread, so it holds no per-message state.
struct HeaderFormatterConfigWrapper {
  std::unique_ptr<HeaderFormatterConfigHandleImpl> config_handle_;
  std::unique_ptr<HeaderFormatterConfig> config_;
};

// One formatter and the handle it was created with, for a single HTTP/1 message. The handle is
// declared first so it is destroyed last: a formatter that kept the reference can still use it
// while it is being torn down.
struct HeaderFormatterWrapper {
  std::unique_ptr<HeaderFormatterHandleImpl> handle_;
  std::unique_ptr<HeaderFormatter> formatter_;
};

} // namespace

extern "C" {

envoy_dynamic_module_type_header_formatter_config_module_ptr
envoy_dynamic_module_on_header_formatter_config_new(
    envoy_dynamic_module_type_header_formatter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<HeaderFormatterConfigHandleImpl>(config_envoy_ptr);
  const std::string_view name_view(name.ptr, name.length);
  const std::string_view config_view(config.ptr, config.length);

  const auto& registry = HeaderFormatterConfigFactoryRegistry::getRegistry();
  auto config_factory = registry.find(name_view);
  if (config_factory == registry.end()) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Header formatter config factory not found for name: {}", name_view);
    return nullptr;
  }

  auto formatter_config = config_factory->second->create(*config_handle, config_view);
  if (!formatter_config) {
    DYM_LOG((*config_handle), LogLevel::Warn, "Failed to create header formatter for name: {}",
            name_view);
    return nullptr;
  }

  auto wrapper = std::make_unique<HeaderFormatterConfigWrapper>();
  wrapper->config_handle_ = std::move(config_handle);
  wrapper->config_ = std::move(formatter_config);
  return wrapPointer(wrapper.release());
}

void envoy_dynamic_module_on_header_formatter_config_destroy(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr) {
  auto* wrapper = unwrapPointer<HeaderFormatterConfigWrapper>(config_module_ptr);
  if (wrapper == nullptr) {
    return;
  }
  if (wrapper->config_) {
    wrapper->config_->onDestroy();
  }
  delete wrapper;
}

envoy_dynamic_module_type_header_formatter_module_ptr envoy_dynamic_module_on_header_formatter_new(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr) {
  auto* wrapper = unwrapPointer<HeaderFormatterConfigWrapper>(config_module_ptr);
  if (wrapper == nullptr || !wrapper->config_) {
    // A null formatter makes Envoy fall back to the default header casing for this message.
    return nullptr;
  }

  // The handle is created before the formatter so it can be handed to it, and outlives every
  // formatter hook, which is why the formatter may keep the reference.
  auto handle = std::make_unique<HeaderFormatterHandleImpl>(formatter_envoy_ptr);
  auto formatter = wrapper->config_->create(*handle);
  if (!formatter) {
    return nullptr;
  }

  auto formatter_wrapper = std::make_unique<HeaderFormatterWrapper>();
  formatter_wrapper->handle_ = std::move(handle);
  formatter_wrapper->formatter_ = std::move(formatter);
  return wrapPointer(formatter_wrapper.release());
}

void envoy_dynamic_module_on_header_formatter_destroy(
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr) {
  delete unwrapPointer<HeaderFormatterWrapper>(formatter_module_ptr);
}

void envoy_dynamic_module_on_header_formatter_process_key(
    // The handle the formatter was created with already wraps this pointer.
    envoy_dynamic_module_type_header_formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key) {
  auto* wrapper = unwrapPointer<HeaderFormatterWrapper>(formatter_module_ptr);
  if (wrapper == nullptr || !wrapper->formatter_) {
    return;
  }
  wrapper->formatter_->processKey(std::string_view(key.ptr, key.length));
}

bool envoy_dynamic_module_on_header_formatter_format(
    // The handle the formatter was created with already wraps this pointer.
    envoy_dynamic_module_type_header_formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_module_buffer* result) {
  auto* wrapper = unwrapPointer<HeaderFormatterWrapper>(formatter_module_ptr);
  if (wrapper == nullptr || !wrapper->formatter_) {
    return false;
  }
  const auto formatted = wrapper->formatter_->format(std::string_view(key.ptr, key.length));
  if (!formatted.has_value()) {
    return false;
  }
  result->ptr = formatted->data();
  result->length = formatted->size();
  return true;
}

} // extern "C"

} // namespace DynamicModules
} // namespace Envoy
