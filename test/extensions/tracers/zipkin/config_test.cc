#include "envoy/registry/registry.h"

#include "extensions/tracers/zipkin/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

TEST(ZipkinTracerConfigTest, ZipkinHttpTracer) {
  NiceMock<Server::MockInstance> server;
  EXPECT_CALL(server.cluster_manager_, get("fake_cluster"))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.zipkin
    config:
      collector_cluster: fake_cluster
      collector_endpoint: /api/v1/spans
  )EOF";

  envoy::config::trace::v2::Tracing configuration;
  MessageUtil::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(configuration, server);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ZipkinTracerFactory, Server::Configuration::TracerFactory>()),
      EnvoyException, "Double registration for name: 'envoy.zipkin'");
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
