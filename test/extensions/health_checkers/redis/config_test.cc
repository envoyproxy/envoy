#include "common/upstream/health_checker_impl.h"

#include "extensions/health_checkers/redis/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

typedef Extensions::HealthCheckers::RedisHealthChecker::RedisHealthChecker CustomRedisHealthChecker;

TEST(HealthCheckerFactoryTest, createRedis) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
        key: foo
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomRedisHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV2Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, createRedisWithoutKey) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomRedisHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV2Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, createRedisViaUpstreamHealthCheckerFactory) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
        key: foo
    )EOF";

  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  AccessLog::MockAccessLogManager log_manager;
  EXPECT_NE(nullptr, dynamic_cast<CustomRedisHealthChecker*>(
                         Upstream::HealthCheckerFactory::create(
                             Upstream::parseHealthCheckFromV2Yaml(yaml), cluster, runtime, random,
                             dispatcher, log_manager)
                             .get()));
}

TEST(HealthCheckerFactoryTest, createRedisWithDeprecatedV1JsonConfig) {
  const std::string json = R"EOF(
    {
      "type": "redis",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 1,
      "healthy_threshold": 1
    }
    )EOF";

  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  AccessLog::MockAccessLogManager log_manager;
  EXPECT_NE(nullptr, dynamic_cast<CustomRedisHealthChecker*>(
                         // Always use Upstream's HealthCheckerFactory when creating instance using
                         // deprecated config.
                         Upstream::HealthCheckerFactory::create(
                             Upstream::parseHealthCheckFromV1Json(json), cluster, runtime, random,
                             dispatcher, log_manager)
                             .get()));
}

TEST(HealthCheckerFactoryTest, createRedisWithDeprecatedV1JsonConfigWithKey) {
  const std::string json = R"EOF(
    {
      "type": "redis",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 1,
      "healthy_threshold": 1,
      "redis_key": "foo"
    }
    )EOF";

  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  AccessLog::MockAccessLogManager log_manager;
  EXPECT_NE(nullptr, dynamic_cast<CustomRedisHealthChecker*>(
                         // Always use Upstream's HealthCheckerFactory when creating instance using
                         // deprecated config.
                         Upstream::HealthCheckerFactory::create(
                             Upstream::parseHealthCheckFromV1Json(json), cluster, runtime, random,
                             dispatcher, log_manager)
                             .get()));
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
