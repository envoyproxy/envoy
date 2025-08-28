#include "envoy/config/trace/v3/http_tracer.pb.h"

#include "source/extensions/tracers/zipkin/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

TEST(ZipkinTracerConfigTest, ZipkinHttpTracer) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  const std::string yaml_string = R"EOF(
  http:
    name: zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: fake_cluster
      collector_endpoint: /api/v2/spans
      collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto zipkin_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, ZipkinHttpTracerWithTypedConfig) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  const std::string yaml_string = R"EOF(
  http:
    name: zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: fake_cluster
      collector_endpoint: /api/v2/spans
      collector_endpoint_version: HTTP_PROTO
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto zipkin_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, ZipkinHttpTracerWithCustomHeaders) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  const std::string yaml_string = R"EOF(
  http:
    name: zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: fake_cluster
      collector_endpoint: /api/v2/spans
      collector_endpoint_version: HTTP_JSON
      collector_request_headers:
        - key: "Authorization"
          value: "Bearer token123"
        - key: "X-Custom-Header"
          value: "custom-value"
        - key: "X-API-Key"
          value: "api-key-123"
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto zipkin_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, zipkin_tracer);
}

TEST(ZipkinTracerConfigTest, ZipkinHttpTracerWithEmptyCustomHeaders) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  const std::string yaml_string = R"EOF(
  http:
    name: zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: fake_cluster
      collector_endpoint: /api/v2/spans
      collector_endpoint_version: HTTP_JSON
      collector_request_headers: []
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  ZipkinTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto zipkin_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, zipkin_tracer);
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
