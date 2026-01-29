#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

DynamicModuleNetworkFilterConfig::DynamicModuleNetworkFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    DynamicModulePtr dynamic_module, Envoy::Upstream::ClusterManager& cluster_manager,
    Stats::Scope& stats_scope, Event::Dispatcher& main_thread_dispatcher)
    : cluster_manager_(cluster_manager), main_thread_dispatcher_(main_thread_dispatcher),
      stats_scope_(stats_scope.createScope(std::string(NetworkFilterStatsNamespace) + ".")),
      stat_name_pool_(stats_scope_->symbolTable()), filter_name_(filter_name),
      filter_config_(filter_config), dynamic_module_(std::move(dynamic_module)) {}

void DynamicModuleNetworkFilterConfig::onScheduled(uint64_t event_id) {
  if (on_network_filter_config_scheduled_ != nullptr) {
    on_network_filter_config_scheduled_(in_module_config_, event_id);
  }
}

DynamicModuleNetworkFilterConfig::~DynamicModuleNetworkFilterConfig() {
  if (in_module_config_ != nullptr && on_network_filter_config_destroy_ != nullptr) {
    on_network_filter_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleNetworkFilterConfigSharedPtr> newDynamicModuleNetworkFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    DynamicModulePtr dynamic_module, Envoy::Upstream::ClusterManager& cluster_manager,
    Stats::Scope& stats_scope, Event::Dispatcher& main_thread_dispatcher) {

  // Resolve the symbols for the network filter using graceful error handling.
  auto on_config_new =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_network_filter_config_new)>(
              "envoy_dynamic_module_on_network_filter_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnNetworkConfigDestroyType>(
      "envoy_dynamic_module_on_network_filter_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_filter_new = dynamic_module->getFunctionPointer<OnNetworkFilterNewType>(
      "envoy_dynamic_module_on_network_filter_new");
  RETURN_IF_NOT_OK_REF(on_filter_new.status());

  auto on_new_connection = dynamic_module->getFunctionPointer<OnNetworkFilterNewConnectionType>(
      "envoy_dynamic_module_on_network_filter_new_connection");
  RETURN_IF_NOT_OK_REF(on_new_connection.status());

  auto on_read = dynamic_module->getFunctionPointer<OnNetworkFilterReadType>(
      "envoy_dynamic_module_on_network_filter_read");
  RETURN_IF_NOT_OK_REF(on_read.status());

  auto on_write = dynamic_module->getFunctionPointer<OnNetworkFilterWriteType>(
      "envoy_dynamic_module_on_network_filter_write");
  RETURN_IF_NOT_OK_REF(on_write.status());

  auto on_event = dynamic_module->getFunctionPointer<OnNetworkFilterEventType>(
      "envoy_dynamic_module_on_network_filter_event");
  RETURN_IF_NOT_OK_REF(on_event.status());

  auto on_destroy = dynamic_module->getFunctionPointer<OnNetworkFilterDestroyType>(
      "envoy_dynamic_module_on_network_filter_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  // HTTP callout done is optional - module may not implement async calls.
  auto on_http_callout_done =
      dynamic_module->getFunctionPointer<OnNetworkFilterHttpCalloutDoneType>(
          "envoy_dynamic_module_on_network_filter_http_callout_done");

  // Optional: modules that don't need scheduling don't need to implement these.
  auto on_filter_scheduled = dynamic_module->getFunctionPointer<OnNetworkFilterScheduledType>(
      "envoy_dynamic_module_on_network_filter_scheduled");
  auto on_config_scheduled = dynamic_module->getFunctionPointer<OnNetworkFilterConfigScheduledType>(
      "envoy_dynamic_module_on_network_filter_config_scheduled");

  // Optional: modules that don't need watermark notifications don't need to implement these.
  auto on_above_write_buffer_high_watermark =
      dynamic_module->getFunctionPointer<OnNetworkFilterAboveWriteBufferHighWatermarkType>(
          "envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark");
  auto on_below_write_buffer_low_watermark =
      dynamic_module->getFunctionPointer<OnNetworkFilterBelowWriteBufferLowWatermarkType>(
          "envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark");

  auto config = std::make_shared<DynamicModuleNetworkFilterConfig>(
      filter_name, filter_config, std::move(dynamic_module), cluster_manager, stats_scope,
      main_thread_dispatcher);

  // Store the resolved function pointers.
  config->on_network_filter_config_destroy_ = on_config_destroy.value();
  config->on_network_filter_new_ = on_filter_new.value();
  config->on_network_filter_new_connection_ = on_new_connection.value();
  config->on_network_filter_read_ = on_read.value();
  config->on_network_filter_write_ = on_write.value();
  config->on_network_filter_event_ = on_event.value();
  config->on_network_filter_destroy_ = on_destroy.value();
  config->on_network_filter_http_callout_done_ =
      on_http_callout_done.ok() ? on_http_callout_done.value() : nullptr;
  config->on_network_filter_scheduled_ =
      on_filter_scheduled.ok() ? on_filter_scheduled.value() : nullptr;
  config->on_network_filter_config_scheduled_ =
      on_config_scheduled.ok() ? on_config_scheduled.value() : nullptr;
  config->on_network_filter_above_write_buffer_high_watermark_ =
      on_above_write_buffer_high_watermark.ok() ? on_above_write_buffer_high_watermark.value()
                                                : nullptr;
  config->on_network_filter_below_write_buffer_low_watermark_ =
      on_below_write_buffer_low_watermark.ok() ? on_below_write_buffer_low_watermark.value()
                                               : nullptr;

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {const_cast<char*>(filter_name.data()),
                                                        filter_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {const_cast<char*>(filter_config.data()),
                                                          filter_config.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buffer, config_buffer);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module network filter config");
  }
  return config;
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
