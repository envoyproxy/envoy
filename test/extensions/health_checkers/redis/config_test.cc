#include "extensions/health_checkers/redis/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

TEST(HealthCheckerFactoryTest, createRedis) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    extension_health_check:
      name: envoy.redis
      config:
        key: foo
    )EOF";

  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;

  RedisHealthCheckerFactory factory;
  EXPECT_NE(nullptr,
            dynamic_cast<RedisHealthChecker*>(
                factory
                    .createExtensionHealthChecker(Upstream::parseHealthCheckFromV2Yaml(yaml),
                                                  cluster, runtime, random, dispatcher)
                    .get()));
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy