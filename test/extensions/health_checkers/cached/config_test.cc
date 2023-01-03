#include <vector>

#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/cached/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {
namespace {

using CustomCachedHealthChecker =
    Extensions::HealthCheckers::CachedHealthChecker::CachedHealthChecker;

TEST(HealthCheckerFactoryTest, CreateCached) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: cached
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.cached.v3.Cached
        host: localhost
        port: 6400
        password: foobared
        db: 100
        tls_options:
          enabled: true
          cacert: /etc/redis/ca.crt
          cert: /etc/redis/client.crt
          key: /etc/redis/client.key
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  CachedHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomCachedHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateCachedWithLogHCFailure) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: cached
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.cached.v3.Cached
        host: localhost
        port: 6400
        password: foobared
        db: 100
        tls_options:
          enabled: true
          cacert: /etc/redis/ca.crt
          cert: /etc/redis/client.crt
          key: /etc/redis/client.key
    always_log_health_check_failures: true
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  CachedHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomCachedHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateCachedViaUpstreamHealthCheckerFactory) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: cached
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.cached.v3.Cached
        host: localhost
        port: 6400
        password: foobared
        db: 100
        tls_options:
          enabled: true
          cacert: /etc/redis/ca.crt
          cert: /etc/redis/client.crt
          key: /etc/redis/client.key
    )EOF";

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  Runtime::MockLoader runtime;
  Random::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  AccessLog::MockAccessLogManager log_manager;
  NiceMock<Api::MockApi> api;
  class FakeSingletonManager : public Singleton::Manager {
  public:
    Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb) override {
      return nullptr;
    }
  };
  FakeSingletonManager fsm;

  EXPECT_NE(nullptr,
            dynamic_cast<CustomCachedHealthChecker*>(
                Upstream::HealthCheckerFactory::create(
                    Upstream::parseHealthCheckFromV3Yaml(yaml), cluster, runtime, dispatcher,
                    log_manager, ProtobufMessage::getStrictValidationVisitor(), api, fsm)
                    .get()));
}

} // namespace
} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
