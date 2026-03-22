#include "source/extensions/filters/udp/dynamic_modules/filter_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

DynamicModuleUdpListenerFilterConfig::DynamicModuleUdpListenerFilterConfig(
    const envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter&
        config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope)
    : filter_name_(config.filter_name()),
      filter_config_(MessageUtil::getJsonStringFromMessageOrError(config.filter_config())),
      dynamic_module_(std::move(dynamic_module)),
      stats_scope_(stats_scope.createScope(
          absl::StrCat(config.dynamic_module_config().metrics_namespace().empty()
                           ? std::string(DefaultMetricsNamespace)
                           : config.dynamic_module_config().metrics_namespace(),
                       ".", config.filter_name(), "."))),
      stat_name_pool_(stats_scope_->symbolTable()) {

  auto config_new_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_config_new_)>(
      "envoy_dynamic_module_on_udp_listener_filter_config_new");
  if (!config_new_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP listener filters: " +
                         std::string(config_new_or_error.status().message()));
  }
  on_filter_config_new_ = config_new_or_error.value();

  auto config_destroy_or_error =
      dynamic_module_->getFunctionPointer<decltype(on_filter_config_destroy_)>(
          "envoy_dynamic_module_on_udp_listener_filter_config_destroy");
  if (!config_destroy_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP listener filters: " +
                         std::string(config_destroy_or_error.status().message()));
  }
  on_filter_config_destroy_ = config_destroy_or_error.value();

  auto filter_new_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_new_)>(
      "envoy_dynamic_module_on_udp_listener_filter_new");
  if (!filter_new_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP listener filters: " +
                         std::string(filter_new_or_error.status().message()));
  }
  on_filter_new_ = filter_new_or_error.value();

  auto filter_on_data_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_on_data_)>(
      "envoy_dynamic_module_on_udp_listener_filter_on_data");
  if (!filter_on_data_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP listener filters: " +
                         std::string(filter_on_data_or_error.status().message()));
  }
  on_filter_on_data_ = filter_on_data_or_error.value();

  auto filter_destroy_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_destroy_)>(
      "envoy_dynamic_module_on_udp_listener_filter_destroy");
  if (!filter_destroy_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP listener filters: " +
                         std::string(filter_destroy_or_error.status().message()));
  }
  on_filter_destroy_ = filter_destroy_or_error.value();

  in_module_config_ =
      on_filter_config_new_(static_cast<void*>(this), {filter_name_.c_str(), filter_name_.size()},
                            {filter_config_.data(), filter_config_.size()});
}

DynamicModuleUdpListenerFilterConfig::~DynamicModuleUdpListenerFilterConfig() {
  if (in_module_config_ != nullptr) {
    on_filter_config_destroy_(in_module_config_);
  }
}

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
