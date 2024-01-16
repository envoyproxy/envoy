#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/redis/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {
namespace {

using CustomRedisHealthChecker = Extensions::HealthCheckers::RedisHealthChecker::RedisHealthChecker;

TEST(HealthCheckerFactoryTest, CreateRedis) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.redis.v3.Redis
        key: foo
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomRedisHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateRedisWithoutKey) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.redis.v3.Redis
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomRedisHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateRedisWithLogHCFailure) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.redis.v3.Redis
    always_log_health_check_failures: true
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomRedisHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateRedisViaUpstreamHealthCheckerFactory) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.redis.v3.Redis
        key: foo
    )EOF";

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_NE(nullptr, dynamic_cast<CustomRedisHealthChecker*>(
                         Upstream::HealthCheckerFactory::create(
                             Upstream::parseHealthCheckFromV3Yaml(yaml), cluster, server_context)
                             .value()
                             .get()));
}
} // namespace
} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
