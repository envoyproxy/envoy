#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/tracers/opencensus/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

TEST(OpenCensusTracerConfigTest, OpenCensusHttpTracer) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*message, context);
  EXPECT_NE(nullptr, tracer);
}

TEST(OpenCensusTracerConfigTest, OpenCensusHttpTracerWithTypedConfig) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 123
        max_number_of_attributes: 12
        max_number_of_annotations: 34
        max_number_of_message_events: 56
        max_number_of_links: 78
      stdout_exporter_enabled: true
      stackdriver_exporter_enabled: true
      stackdriver_project_id: test_project_id
      zipkin_exporter_enabled: true
      zipkin_url: http://127.0.0.1:9411/api/v2/spans
      ocagent_exporter_enabled: true
      ocagent_address: 127.0.0.1:55678
      incoming_trace_context: b3
      incoming_trace_context: trace_context
      incoming_trace_context: grpc_trace_bin
      incoming_trace_context: cloud_trace_context
      outgoing_trace_context: trace_context
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*message, context);
  EXPECT_NE(nullptr, tracer);

  // Reset TraceParams back to default.
  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {32, 32, 128, 32, ::opencensus::trace::ProbabilitySampler(1e-4)});
}

TEST(OpenCensusTracerConfigTest, OpenCensusHttpTracerGrpc) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 123
        max_number_of_attributes: 12
        max_number_of_annotations: 34
        max_number_of_message_events: 56
        max_number_of_links: 78
      ocagent_exporter_enabled: true
      ocagent_grpc_service:
        google_grpc:
          target_uri: 127.0.0.1:55678
          stat_prefix: test
      incoming_trace_context: b3
      incoming_trace_context: trace_context
      incoming_trace_context: grpc_trace_bin
      incoming_trace_context: cloud_trace_context
      outgoing_trace_context: trace_context
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr tracer = factory.createHttpTracer(*message, context);
  EXPECT_NE(nullptr, tracer);

  // Reset TraceParams back to default.
  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {32, 32, 128, 32, ::opencensus::trace::ProbabilitySampler(1e-4)});
}

TEST(OpenCensusTracerConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<OpenCensusTracerFactory, Server::Configuration::TracerFactory>()),
      EnvoyException, "Double registration for name: 'envoy.tracers.opencensus'");
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
