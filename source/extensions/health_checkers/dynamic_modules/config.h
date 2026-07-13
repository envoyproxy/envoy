#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/health_checkers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/health_checkers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/health_checker_config.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

/**
 * Config registration for the dynamic module health checker.
 */
class DynamicModuleHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.dynamic_modules"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::health_checkers::dynamic_modules::v3::DynamicModuleHealthCheck>();
  }
};

DECLARE_FACTORY(DynamicModuleHealthCheckerFactory);

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
