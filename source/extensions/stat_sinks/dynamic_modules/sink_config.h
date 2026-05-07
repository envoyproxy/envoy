#pragma once

#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnStatSinkConfigNewType = decltype(&envoy_dynamic_module_on_stat_sink_config_new);
using OnStatSinkConfigDestroyType = decltype(&envoy_dynamic_module_on_stat_sink_config_destroy);
using OnStatSinkFlushType = decltype(&envoy_dynamic_module_on_stat_sink_flush);
using OnStatSinkOnHistogramCompleteType =
    decltype(&envoy_dynamic_module_on_stat_sink_on_histogram_complete);

/**
 * Configuration for a dynamic module stats sink. Holds the resolved module symbols and
 * the in-module config pointer. Multiple sink instances share this config object.
 *
 * Symbol resolution and in-module config creation are done in newDynamicModuleStatsSinkConfig()
 * to allow graceful error handling.
 */
class DynamicModuleStatsSinkConfig {
public:
  /**
   * @param sink_name the name identifying the sink implementation within the module.
   * @param sink_config the configuration bytes for the sink.
   * @param dynamic_module the loaded dynamic module.
   */
  DynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module);
  ~DynamicModuleStatsSinkConfig();

  // The corresponding in-module configuration pointer.
  envoy_dynamic_module_type_stat_sink_config_module_ptr in_module_config_{nullptr};

  // Required function pointers resolved during newDynamicModuleStatsSinkConfig().
  OnStatSinkConfigDestroyType on_config_destroy_{nullptr};
  OnStatSinkFlushType on_flush_{nullptr};
  OnStatSinkOnHistogramCompleteType on_histogram_complete_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleStatsSinkConfig>>
  newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string sink_name_;
  const std::string sink_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleStatsSinkConfigSharedPtr = std::shared_ptr<DynamicModuleStatsSinkConfig>;

/**
 * Creates a new DynamicModuleStatsSinkConfig.
 * @param sink_name the name identifying the sink implementation within the module.
 * @param sink_config the configuration bytes for the sink.
 * @param dynamic_module the loaded dynamic module.
 * @return a shared pointer to the config or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleStatsSinkConfigSharedPtr>
newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
