#include <vector>

#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/thrift/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {
namespace {

using CustomThriftHealthChecker =
    Extensions::HealthCheckers::ThriftHealthChecker::ThriftHealthChecker;

TEST(HealthCheckerFactoryTest, CreateThrift) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  ThriftHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomThriftHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateThriftMissingField) {
  // clang-format off
  std::vector<std::string> yamls = {
  // missing method name
  R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        transport: HEADER
        protocol: BINARY
  )EOF",
  // missing transport
  R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        protocol: BINARY
  )EOF",
  // missing protocol
  R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
  )EOF",
  // AUTO protocol is not allowed.
  R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        protocol: AUTO
  )EOF",
  // AUTO transport is not allowed.
  R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: AUTO
  )EOF"};
  // clang-format on

  ThriftHealthCheckerFactory factory;

  for (const std::string& yaml : yamls) {
    NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

    EXPECT_THROW(
        factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context),
        EnvoyException);
  }
}

TEST(HealthCheckerFactoryTest, CreateThriftWithLogHCFailure) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    always_log_health_check_failures: true
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  ThriftHealthCheckerFactory factory;
  EXPECT_NE(
      nullptr,
      dynamic_cast<CustomThriftHealthChecker*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(HealthCheckerFactoryTest, CreateThriftViaUpstreamHealthCheckerFactory) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    )EOF";

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_NE(nullptr, dynamic_cast<CustomThriftHealthChecker*>(
                         Upstream::HealthCheckerFactory::create(
                             Upstream::parseHealthCheckFromV3Yaml(yaml), cluster, server_context)
                             .value()
                             .get()));
}

} // namespace
} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
