#include "extensions/health_checkers/redis_health_checker/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

envoy::api::v2::core::HealthCheck parseHealthCheckFromYaml(const std::string& yaml_string) {
  envoy::api::v2::core::HealthCheck health_check;
  MessageUtil::loadFromYaml(yaml_string, health_check);
  return health_check;
}

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

  const auto& hc_config = parseHealthCheckFromYaml(yaml);

  RedisHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<Extensions::HealthCheckers::RedisHealthChecker::RedisHealthCheckerImpl*>(
          factory.createExtensionHealthChecker(hc_config, cluster, runtime, random, dispatcher)
              .get()));
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy