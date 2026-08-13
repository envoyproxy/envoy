#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/config.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithGrpcExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        grpc_service:
          envoy_grpc:
            cluster_name: fake_cluster
          timeout: 0.250s
        service_name: fake_service_name
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto opentelemetry_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, opentelemetry_tracer);
}

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithHttpExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        http_service:
          http_uri:
            uri: "https://some-o11y.com//otlp/v1/traces"
            cluster: "my_o11y_backend"
            timeout: 0.250s
          request_headers_to_add:
          - header:
              key: "Authorization"
              value: "auth-token"
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto opentelemetry_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, opentelemetry_tracer);
}

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerNoExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(*message, context), EnvoyException,
                          "Proto constraint validation failed");
}

namespace {

class DummyTraceExporter : public OpenTelemetryTraceExporter {
public:
  bool log(const ExportTraceServiceRequest& /*request*/) override { return true; }
};

class DummyTraceExporterFactory : public OpenTelemetryTraceExporterFactory {
public:
  OpenTelemetryTraceExporterPtr
  createExporter(const Protobuf::Message& config,
                 Server::Configuration::TracerFactoryContext& /*context*/) const override {
    EXPECT_NE(dynamic_cast<const ProtobufWkt::Empty*>(&config), nullptr);
    return std::make_unique<DummyTraceExporter>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Empty>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.exporters.dummy_config_test";
  }
};

REGISTER_FACTORY(DummyTraceExporterFactory, OpenTelemetryTraceExporterFactory);

class NullConfigTraceExporterFactory : public OpenTelemetryTraceExporterFactory {
public:
  OpenTelemetryTraceExporterPtr
  createExporter(const Protobuf::Message& /*config*/,
                 Server::Configuration::TracerFactoryContext& /*context*/) const override {
    return std::make_unique<DummyTraceExporter>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }

  std::string name() const override { return "envoy.tracers.opentelemetry.exporters.null_config"; }

  std::set<std::string> configTypes() override { return {"google.protobuf.Struct"}; }
};

REGISTER_FACTORY(NullConfigTraceExporterFactory, OpenTelemetryTraceExporterFactory);

} // namespace

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithCustomExporter) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        exporter:
          name: envoy.tracers.opentelemetry.exporters.dummy_config_test
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Empty
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);

  auto opentelemetry_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, opentelemetry_tracer);
}

TEST(OpenTelemetryTracerConfigTest, OpenTelemetryTracerWithCustomExporterNullConfigProto) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  OpenTelemetryTracerFactory factory;

  const std::string yaml_string = R"EOF(
    http:
      name: envoy.tracers.opentelemetry
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
        exporter:
          name: envoy.tracers.opentelemetry.exporters.null_config
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);

  EXPECT_THROW_WITH_MESSAGE(factory.createTracerDriver(*message, context), EnvoyException,
                            "OpenTelemetry trace exporter factory "
                            "'envoy.tracers.opentelemetry.exporters.null_config' "
                            "returned nullptr from createEmptyConfigProto()");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
