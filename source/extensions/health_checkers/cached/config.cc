#include "source/extensions/health_checkers/cached/config.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/health_checkers/cached/utility.h"
#include "source/extensions/health_checkers/cached/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

Upstream::HealthCheckerSharedPtr CachedHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<CachedHealthChecker>(
      context.cluster(), config,
      getCachedHealthCheckConfig(config, context.messageValidationVisitor()),
      context.mainThreadDispatcher(), context.runtime(), context.eventLogger(), context.api(),
      context.singletonManager(), ClientFactoryImpl::instance_);
};

/**
 * Static registration for the cached custom health checker. @see RegisterFactory.
 */
REGISTER_FACTORY(CachedHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
