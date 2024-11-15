#include "source/extensions/health_checkers/thrift/config.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/health_checkers/thrift/client_impl.h"
#include "source/extensions/health_checkers/thrift/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

Upstream::HealthCheckerSharedPtr ThriftHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<ThriftHealthChecker>(
      context.cluster(), config,
      getThriftHealthCheckConfig(config, context.messageValidationVisitor()),
      context.mainThreadDispatcher(), context.runtime(), context.eventLogger(), context.api(),
      ClientFactoryImpl::instance_);
};

/**
 * Static registration for the thrift custom health checker. @see RegisterFactory.
 */
REGISTER_FACTORY(ThriftHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
