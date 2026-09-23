#include "source/extensions/http/header_formatters/dynamic_modules/header_formatter.h"

#include <string>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {

DynamicModuleHeaderFormatter::DynamicModuleHeaderFormatter(
    DynamicModuleHeaderFormatterConfigSharedPtr config)
    : config_(std::move(config)) {}

bool DynamicModuleHeaderFormatter::initializeInModuleFormatter() {
  ASSERT(in_module_formatter_ == nullptr);
  in_module_formatter_ = config_->on_formatter_new_(config_->in_module_config_, thisAsVoidPtr());
  return in_module_formatter_ != nullptr;
}

DynamicModuleHeaderFormatter::~DynamicModuleHeaderFormatter() {
  // Null when the module declined to create a formatter, in which case there is nothing to destroy
  // and this object never escaped create().
  if (in_module_formatter_ != nullptr) {
    config_->on_formatter_destroy_(in_module_formatter_);
  }
}

std::string DynamicModuleHeaderFormatter::format(absl::string_view key) const {
  envoy_dynamic_module_type_envoy_buffer key_buffer = {.ptr = key.data(), .length = key.size()};
  envoy_dynamic_module_type_module_buffer result = {nullptr, 0};
  if (!config_->on_format_(thisAsVoidPtr(), in_module_formatter_, key_buffer, &result)) {
    return std::string(key);
  }
  // An empty result is treated as "unchanged" rather than as an empty key: a header with an empty
  // key cannot be serialized, so echoing the key back is the only safe reading of it.
  if (result.ptr == nullptr || result.length == 0) {
    return std::string(key);
  }
  return std::string(result.ptr, result.length);
}

void DynamicModuleHeaderFormatter::processKey(absl::string_view key) {
  envoy_dynamic_module_type_envoy_buffer key_buffer = {.ptr = key.data(), .length = key.size()};
  config_->on_process_key_(thisAsVoidPtr(), in_module_formatter_, key_buffer);
}

void DynamicModuleHeaderFormatter::setReasonPhrase(absl::string_view) {}

absl::string_view DynamicModuleHeaderFormatter::getReasonPhrase() const { return {}; }

DynamicModuleHeaderFormatterConfig::DynamicModuleHeaderFormatterConfig(
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleHeaderFormatterConfig::~DynamicModuleHeaderFormatterConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

Envoy::Http::StatefulHeaderKeyFormatterPtr DynamicModuleHeaderFormatterConfig::create() {
  auto formatter = std::make_unique<DynamicModuleHeaderFormatter>(shared_from_this());
  if (!formatter->initializeInModuleFormatter()) {
    // This runs once per HTTP/1 message, so a module that always declines would otherwise log on
    // every request.
    ENVOY_LOG_EVERY_POW_2(warn, "Dynamic module header formatter declined to create a formatter; "
                                "falling back to the default header casing");
    return nullptr;
  }
  return formatter;
}

absl::StatusOr<DynamicModuleHeaderFormatterConfigSharedPtr>
newDynamicModuleHeaderFormatterConfig(absl::string_view formatter_name,
                                      absl::string_view formatter_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                      Event::Dispatcher& main_thread_dispatcher) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  // A missing ABI symbol is a module-level problem, so it is reported as NotFound and the caller
  // can distinguish it from a configuration that the module itself rejected.
  auto on_config_new = dynamic_module->getFunctionPointer<OnHeaderFormatterConfigNewType>(
      "envoy_dynamic_module_on_header_formatter_config_new");
  if (!on_config_new.ok()) {
    return absl::NotFoundError(on_config_new.status().message());
  }

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnHeaderFormatterConfigDestroyType>(
      "envoy_dynamic_module_on_header_formatter_config_destroy");
  if (!on_config_destroy.ok()) {
    return absl::NotFoundError(on_config_destroy.status().message());
  }

  auto on_formatter_new = dynamic_module->getFunctionPointer<OnHeaderFormatterNewType>(
      "envoy_dynamic_module_on_header_formatter_new");
  if (!on_formatter_new.ok()) {
    return absl::NotFoundError(on_formatter_new.status().message());
  }

  auto on_formatter_destroy = dynamic_module->getFunctionPointer<OnHeaderFormatterDestroyType>(
      "envoy_dynamic_module_on_header_formatter_destroy");
  if (!on_formatter_destroy.ok()) {
    return absl::NotFoundError(on_formatter_destroy.status().message());
  }

  auto on_process_key = dynamic_module->getFunctionPointer<OnHeaderFormatterProcessKeyType>(
      "envoy_dynamic_module_on_header_formatter_process_key");
  if (!on_process_key.ok()) {
    return absl::NotFoundError(on_process_key.status().message());
  }

  auto on_format = dynamic_module->getFunctionPointer<OnHeaderFormatterFormatType>(
      "envoy_dynamic_module_on_header_formatter_format");
  if (!on_format.ok()) {
    return absl::NotFoundError(on_format.status().message());
  }

  // A worker thread can drop the last reference: the formatters handed out by create() keep the
  // configuration alive and are owned by header maps that may outlive the protocol options. Route
  // the deletion through the main thread dispatcher so that the in-module destroy hook and the
  // dlclose() behind dynamic_module_ always run on the main thread, as the ABI documents.
  // deleteInDispatcherThread() may be called from any thread, and the dispatcher drains what it
  // has been handed on its own thread before it goes away.
  DynamicModuleHeaderFormatterConfigSharedPtr config(
      new DynamicModuleHeaderFormatterConfig(std::move(dynamic_module)),
      [&main_thread_dispatcher](DynamicModuleHeaderFormatterConfig* config) {
        main_thread_dispatcher.deleteInDispatcherThread(
            std::unique_ptr<const Event::DispatcherThreadDeletable>(config));
      });
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_formatter_new_ = on_formatter_new.value();
  config->on_formatter_destroy_ = on_formatter_destroy.value();
  config->on_process_key_ = on_process_key.value();
  config->on_format_ = on_format.value();

  // The module is handed the caller's buffers directly: the ABI only promises them for the
  // duration of the hook, so a module that needs the name or the configuration later copies it.
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = formatter_name.data(),
                                                     .length = formatter_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = formatter_config.data(),
                                                       .length = formatter_config.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module header formatter config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
