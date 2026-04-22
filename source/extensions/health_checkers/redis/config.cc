#include "source/extensions/health_checkers/redis/config.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/health_checkers/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

Upstream::HealthCheckerSharedPtr RedisHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {

  auto redis_config = getRedisHealthCheckConfig(config, context.messageValidationVisitor());

  absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config;
  if (redis_config.has_aws_iam()) {
    aws_iam_config = redis_config.aws_iam();
    aws_iam_authenticator_ =
        NetworkFilters::Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorFactory::
            initAwsIamAuthenticator(context.serverFactoryContext(), redis_config.aws_iam());
  }

  return std::make_shared<RedisHealthChecker>(
      context.cluster(), config,
      getRedisHealthCheckConfig(config, context.messageValidationVisitor()),
      context.mainThreadDispatcher(), context.runtime(), context.eventLogger(), context.api(),
      NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_, aws_iam_config,
      aws_iam_authenticator_);
};

/**
 * Static registration for the redis custom health checker. @see RegisterFactory.
 */
REGISTER_FACTORY(RedisHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
