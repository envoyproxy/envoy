#include "source/extensions/health_checkers/dynamic_modules/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

Upstream::HealthCheckerSharedPtr DynamicModuleHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  envoy::extensions::health_checkers::dynamic_modules::v3::DynamicModuleHealthCheck proto_config;
  THROW_IF_NOT_OK(Envoy::Config::Utility::translateOpaqueConfig(
      config.custom_health_check().typed_config(), context.messageValidationVisitor(),
      proto_config));

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module_or_error.status().message()));
  }

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string health_checker_config_str;
  if (proto_config.has_health_checker_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.health_checker_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse health checker config: " +
                           std::string(config_or_error.status().message()));
    }
    health_checker_config_str = std::move(config_or_error.value());
  }

  auto module_health_checker_config = newDynamicModuleHealthCheckerConfig(
      proto_config.health_checker_name(), health_checker_config_str,
      std::move(dynamic_module_or_error.value()));
  if (!module_health_checker_config.ok()) {
    throw EnvoyException("Failed to create dynamic module health checker config: " +
                         std::string(module_health_checker_config.status().message()));
  }

  return std::make_shared<DynamicModuleHealthChecker>(
      context.cluster(), config, std::move(module_health_checker_config.value()),
      context.mainThreadDispatcher(), context.runtime(), context.api().randomGenerator(),
      context.eventLogger());
}

/**
 * Static registration for the dynamic module health checker. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamicModuleHealthCheckerFactory,
                 Server::Configuration::CustomHealthCheckerFactory);

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
