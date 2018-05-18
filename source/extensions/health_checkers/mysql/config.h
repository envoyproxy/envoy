#pragma once

#include "envoy/server/health_checker_config.h"

#include "extensions/health_checkers/mysql/mysql.h"
#include "extensions/health_checkers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

/**
 * Config registration for the mysql health checker.
 */
class MySQLHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::api::v2::core::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() override { return HealthCheckerNames::get().MysqlHealthChecker; }
};

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
