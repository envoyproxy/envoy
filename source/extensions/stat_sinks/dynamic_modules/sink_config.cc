#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSinkConfig::DynamicModuleStatsSinkConfig(
    absl::string_view sink_name, absl::string_view sink_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : sink_name_(sink_name), sink_config_(sink_config), dynamic_module_(std::move(dynamic_module)) {
}

DynamicModuleStatsSinkConfig::~DynamicModuleStatsSinkConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleStatsSinkConfigSharedPtr>
newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnStatSinkConfigNewType>(
      "envoy_dynamic_module_on_stat_sink_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnStatSinkConfigDestroyType>(
      "envoy_dynamic_module_on_stat_sink_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_flush = dynamic_module->getFunctionPointer<OnStatSinkFlushType>(
      "envoy_dynamic_module_on_stat_sink_flush");
  RETURN_IF_NOT_OK_REF(on_flush.status());

  auto on_histogram_complete =
      dynamic_module->getFunctionPointer<OnStatSinkOnHistogramCompleteType>(
          "envoy_dynamic_module_on_stat_sink_on_histogram_complete");
  RETURN_IF_NOT_OK_REF(on_histogram_complete.status());

  auto config = std::make_shared<DynamicModuleStatsSinkConfig>(sink_name, sink_config,
                                                               std::move(dynamic_module));

  config->on_config_destroy_ = on_config_destroy.value();
  config->on_flush_ = on_flush.value();
  config->on_histogram_complete_ = on_histogram_complete.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->sink_name_.data(),
                                                     .length = config->sink_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->sink_config_.data(),
                                                       .length = config->sink_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module stats sink config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
