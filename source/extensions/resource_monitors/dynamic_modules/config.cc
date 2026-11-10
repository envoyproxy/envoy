#include "source/extensions/resource_monitors/dynamic_modules/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/resource_monitors/dynamic_modules/resource_monitor.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {

absl::StatusOr<Server::ResourceMonitorPtr>
DynamicModuleResourceMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::dynamic_modules::v3::DynamicModuleResourceMonitor&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  // Resource monitors have no factory context, so only the synchronous local-file and by-name
  // module sources can succeed here. A remote source is rejected by the loader.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      config.dynamic_module_config(), config.resource_monitor_name());
  RETURN_IF_NOT_OK_REF(load_result.status());

  std::string resource_monitor_config;
  if (config.has_resource_monitor_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(config.resource_monitor_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("Failed to parse resource monitor config: ",
                                                     config_or_error.status().message()));
    }
    resource_monitor_config = std::move(config_or_error.value());
  }

  auto monitor_config = newDynamicModuleResourceMonitorConfig(
      config.resource_monitor_name(), resource_monitor_config, std::move(load_result->loaded),
      context.mainThreadDispatcher());
  RETURN_IF_NOT_OK_REF(monitor_config.status());
  if (monitor_config.value()->synchronous_feedback_enabled_) {
    return std::make_unique<DynamicModuleSynchronousFeedbackResourceMonitor>(
        std::move(monitor_config.value()));
  }
  return std::make_unique<DynamicModuleResourceMonitor>(std::move(monitor_config.value()));
}

/**
 * Static registration for the dynamic module resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(DynamicModuleResourceMonitorFactory,
                 Server::Configuration::ResourceMonitorFactory);

} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
