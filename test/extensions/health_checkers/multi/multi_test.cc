#include "source/extensions/health_checkers/multi/multi.h"

#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/priority_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {
namespace {

using ::testing::NiceMock;

TEST(MultiHealthCheckerFactoryTest, CreateFromValidConfig) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.multi
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
        health_checks:
        - timeout: 1s
          interval: 1s
          unhealthy_threshold: 2
          healthy_threshold: 2
          http_health_check:
            path: /healthcheck
        - timeout: 1s
          interval: 1s
          unhealthy_threshold: 3
          healthy_threshold: 3
          tcp_health_check: {}
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  MultiHealthCheckerFactory factory;
  auto checker = factory.createCustomHealthChecker(
      Upstream::parseHealthCheckFromV3Yaml(yaml), context);
  EXPECT_NE(nullptr, checker.get());

  auto* multi = dynamic_cast<MultiHealthChecker*>(checker.get());
  ASSERT_NE(nullptr, multi);
  EXPECT_EQ(2u, multi->numCheckers());
}

TEST(MultiHealthCheckerFactoryTest, FactoryName) {
  MultiHealthCheckerFactory factory;
  EXPECT_EQ("envoy.health_checkers.multi", factory.name());
}

} // namespace
} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
