#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

DynamicModuleListenerFilterConfig::DynamicModuleListenerFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    Event::Dispatcher& main_thread_dispatcher)
    : main_thread_dispatcher_(main_thread_dispatcher),
      stats_scope_(stats_scope.createScope(std::string(ListenerFilterStatsNamespace) + ".")),
      stat_name_pool_(stats_scope_->symbolTable()), filter_name_(filter_name),
      filter_config_(filter_config), dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleListenerFilterConfig::~DynamicModuleListenerFilterConfig() {
  if (in_module_config_ != nullptr) {
    on_listener_filter_config_destroy_(in_module_config_);
  }
}

void DynamicModuleListenerFilterConfig::onScheduled(uint64_t event_id) {
  if (on_listener_filter_config_scheduled_) {
    (*on_listener_filter_config_scheduled_)(this, in_module_config_, event_id);
  }
}

absl::StatusOr<DynamicModuleListenerFilterConfigSharedPtr>
newDynamicModuleListenerFilterConfig(const absl::string_view filter_name,
                                     const absl::string_view filter_config,
                                     DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
                                     Event::Dispatcher& main_thread_dispatcher) {

  // Resolve the symbols for the listener filter using graceful error handling.
  auto on_config_new =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_listener_filter_config_new)>(
              "envoy_dynamic_module_on_listener_filter_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnListenerConfigDestroyType>(
      "envoy_dynamic_module_on_listener_filter_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_filter_new = dynamic_module->getFunctionPointer<OnListenerFilterNewType>(
      "envoy_dynamic_module_on_listener_filter_new");
  RETURN_IF_NOT_OK_REF(on_filter_new.status());

  auto on_accept = dynamic_module->getFunctionPointer<OnListenerFilterOnAcceptType>(
      "envoy_dynamic_module_on_listener_filter_on_accept");
  RETURN_IF_NOT_OK_REF(on_accept.status());

  auto on_data = dynamic_module->getFunctionPointer<OnListenerFilterOnDataType>(
      "envoy_dynamic_module_on_listener_filter_on_data");
  RETURN_IF_NOT_OK_REF(on_data.status());

  auto on_close = dynamic_module->getFunctionPointer<OnListenerFilterOnCloseType>(
      "envoy_dynamic_module_on_listener_filter_on_close");
  RETURN_IF_NOT_OK_REF(on_close.status());

  auto on_get_max_read_bytes =
      dynamic_module->getFunctionPointer<OnListenerFilterGetMaxReadBytesType>(
          "envoy_dynamic_module_on_listener_filter_get_max_read_bytes");
  RETURN_IF_NOT_OK_REF(on_get_max_read_bytes.status());

  auto on_destroy = dynamic_module->getFunctionPointer<OnListenerFilterDestroyType>(
      "envoy_dynamic_module_on_listener_filter_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  auto on_scheduled = dynamic_module->getFunctionPointer<OnListenerFilterScheduledType>(
      "envoy_dynamic_module_on_listener_filter_scheduled");
  RETURN_IF_NOT_OK_REF(on_scheduled.status());

  // This is optional. Modules that don't need config-level scheduling don't need to implement it.
  auto on_config_scheduled =
      dynamic_module->getFunctionPointer<OnListenerFilterConfigScheduledType>(
          "envoy_dynamic_module_on_listener_filter_config_scheduled");

  auto config = std::make_shared<DynamicModuleListenerFilterConfig>(
      filter_name, filter_config, std::move(dynamic_module), stats_scope, main_thread_dispatcher);

  // Store the resolved function pointers.
  config->on_listener_filter_config_destroy_ = on_config_destroy.value();
  config->on_listener_filter_new_ = on_filter_new.value();
  config->on_listener_filter_on_accept_ = on_accept.value();
  config->on_listener_filter_on_data_ = on_data.value();
  config->on_listener_filter_on_close_ = on_close.value();
  config->on_listener_filter_get_max_read_bytes_ = on_get_max_read_bytes.value();
  config->on_listener_filter_destroy_ = on_destroy.value();
  config->on_listener_filter_scheduled_ = on_scheduled.value();
  if (on_config_scheduled.ok()) {
    config->on_listener_filter_config_scheduled_ = on_config_scheduled.value();
  }

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {const_cast<char*>(filter_name.data()),
                                                        filter_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {const_cast<char*>(filter_config.data()),
                                                          filter_config.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buffer, config_buffer);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module listener filter config");
  }
  return config;
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
