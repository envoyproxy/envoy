#include "envoy/registry/registry.h"

#include "extensions/tracers/zipkin/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Eq;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

TEST(ZipkinTracerConfigTest, ZipkinHttpTracer) {
  NiceMock<Server::MockInstance> server;

  EXPECT_CALL(server.cluster_manager_, get(Eq("fake_cluster")))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.zipkin
    config:
      collector_cluster: fake_cluster
      collector_endpoint: /api/v1/spans
      collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v2::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(*message, server);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, ZipkinHttpTracerWithTypedConfig) {
  NiceMock<Server::MockInstance> server;

  EXPECT_CALL(server.cluster_manager_, get(Eq("fake_cluster")))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.ZipkinConfig
      collector_cluster: fake_cluster
      collector_endpoint: /api/v2/spans
      collector_endpoint_version: HTTP_PROTO
  )EOF";

  envoy::config::trace::v2::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(*message, server);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ZipkinTracerFactory, Server::Configuration::TracerFactory>()),
      EnvoyException, "Double registration for name: 'envoy.zipkin'");
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
