#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

DynamicModuleUdpSessionFilterConfig::DynamicModuleUdpSessionFilterConfig(
    const envoy::extensions::filters::udp::udp_proxy::session::dynamic_modules::v3::
        DynamicModuleSessionFilter& config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    TimeSource& time_source)
    : filter_name_(config.filter_name()),
      filter_config_(
          THROW_OR_RETURN_VALUE(MessageUtil::knownAnyToBytes(config.filter_config()), std::string)),
      dynamic_module_(std::move(dynamic_module)),
      stats_scope_(stats_scope.createScope(
          absl::StrCat(config.dynamic_module_config().metrics_namespace().empty()
                           ? std::string(DefaultMetricsNamespace)
                           : config.dynamic_module_config().metrics_namespace(),
                       ".", config.filter_name(), "."))),
      stat_name_pool_(stats_scope_->symbolTable()), time_source_(time_source) {

  auto config_new_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_config_new_)>(
      "envoy_dynamic_module_on_udp_session_filter_config_new");
  if (!config_new_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(config_new_or_error.status().message()));
  }
  on_filter_config_new_ = config_new_or_error.value();

  auto config_destroy_or_error =
      dynamic_module_->getFunctionPointer<decltype(on_filter_config_destroy_)>(
          "envoy_dynamic_module_on_udp_session_filter_config_destroy");
  if (!config_destroy_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(config_destroy_or_error.status().message()));
  }
  on_filter_config_destroy_ = config_destroy_or_error.value();

  auto filter_new_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_new_)>(
      "envoy_dynamic_module_on_udp_session_filter_new");
  if (!filter_new_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(filter_new_or_error.status().message()));
  }
  on_filter_new_ = filter_new_or_error.value();

  auto filter_on_new_session_or_error =
      dynamic_module_->getFunctionPointer<decltype(on_filter_on_new_session_)>(
          "envoy_dynamic_module_on_udp_session_filter_on_new_session");
  if (!filter_on_new_session_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(filter_on_new_session_or_error.status().message()));
  }
  on_filter_on_new_session_ = filter_on_new_session_or_error.value();

  auto filter_on_data_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_on_data_)>(
      "envoy_dynamic_module_on_udp_session_filter_on_data");
  if (!filter_on_data_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(filter_on_data_or_error.status().message()));
  }
  on_filter_on_data_ = filter_on_data_or_error.value();

  auto filter_on_write_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_on_write_)>(
      "envoy_dynamic_module_on_udp_session_filter_on_write");
  if (!filter_on_write_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(filter_on_write_or_error.status().message()));
  }
  on_filter_on_write_ = filter_on_write_or_error.value();

  // on_session_complete is optional; only resolve it if the module exports it.
  auto filter_on_session_complete_or_error =
      dynamic_module_->getFunctionPointer<decltype(on_filter_on_session_complete_)>(
          "envoy_dynamic_module_on_udp_session_filter_on_session_complete");
  if (filter_on_session_complete_or_error.ok()) {
    on_filter_on_session_complete_ = filter_on_session_complete_or_error.value();
  }

  auto filter_destroy_or_error = dynamic_module_->getFunctionPointer<decltype(on_filter_destroy_)>(
      "envoy_dynamic_module_on_udp_session_filter_destroy");
  if (!filter_destroy_or_error.ok()) {
    throw EnvoyException("Dynamic module does not support UDP session filters: " +
                         std::string(filter_destroy_or_error.status().message()));
  }
  on_filter_destroy_ = filter_destroy_or_error.value();

  in_module_config_ =
      on_filter_config_new_(static_cast<void*>(this), {filter_name_.c_str(), filter_name_.size()},
                            {filter_config_.data(), filter_config_.size()});
  if (in_module_config_ == nullptr) {
    throw EnvoyException("Failed to initialize dynamic module UDP session filter config");
  }
  stat_creation_frozen_ = true;
}

DynamicModuleUdpSessionFilterConfig::~DynamicModuleUdpSessionFilterConfig() {
  if (in_module_config_ != nullptr) {
    on_filter_config_destroy_(in_module_config_);
  }
}

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
