#include "extensions/health_checkers/mysql/config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/health_checkers/mysql/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

Upstream::HealthCheckerSharedPtr MySQLHealthCheckerFactory::createCustomHealthChecker(
    const envoy::api::v2::core::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<MySQLHealthChecker>(
      context.cluster(), config, getMySQLHealthCheckConfig(config), context.dispatcher(),
      context.runtime(), context.random(), context.eventLogger());
};

/**
 * Static registration for the mysql custom health checker. @see RegisterFactory.
 */
static Registry::RegisterFactory<MySQLHealthCheckerFactory,
                                 Server::Configuration::CustomHealthCheckerFactory>
    registered_;

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
