#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opencensus/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(InvalidStackdriverConfiguration)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      stackdriver_exporter_enabled: true
      stackdriver_grpc_service:
        envoy_grpc:
          cluster_name: stackdriver
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_MESSAGE((factory.createTracerDriver(*message, context)), EnvoyException,
                            "Opencensus stackdriver tracer only support GoogleGrpc.");
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(InvalidOcagentConfiguration)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      ocagent_exporter_enabled: true
      ocagent_grpc_service:
        envoy_grpc:
          cluster_name: opencensus
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_MESSAGE((factory.createTracerDriver(*message, context)), EnvoyException,
                            "Opencensus ocagent tracer only supports GoogleGrpc.");
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(OpenCensusHttpTracer)) {
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
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(OpenCensusHttpTracerWithTypedConfig)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
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
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);

  // Reset TraceParams back to default.
  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {32, 32, 128, 32, ::opencensus::trace::ProbabilitySampler(1e-4)});
}

TEST(OpenCensusTracerConfigTest,
     DEPRECATED_FEATURE_TEST(OpenCensusHttpTracerWithDeprecatedTypedConfig)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 123
      zipkin_exporter_enabled: true
      zipkin_url: http://127.0.0.1:9411/api/v2/spans
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
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);

  // Reset TraceParams back to default.
  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {32, 32, 128, 32, ::opencensus::trace::ProbabilitySampler(1e-4)});
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(OpenCensusHttpTracerGrpc)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
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
#ifdef ENVOY_GOOGLE_GRPC
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);

  // Reset TraceParams back to default.
  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {32, 32, 128, 32, ::opencensus::trace::ProbabilitySampler(1e-4)});
#else
  EXPECT_THROW_WITH_MESSAGE(
      (factory.createTracerDriver(*message, context)), EnvoyException,
      "Opencensus tracer: cannot handle ocagent google grpc service, google grpc is not built in.");
#endif
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(ShouldCreateAtMostOneOpenCensusTracer)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 123
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message_one = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer_one = factory.createTracerDriver(*message_one, context);
  EXPECT_NE(nullptr, tracer_one);

  auto message_two = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer_two = factory.createTracerDriver(*message_two, context);
  // Verify that no new tracer has been created.
  EXPECT_EQ(tracer_two, tracer_one);
}

TEST(OpenCensusTracerConfigTest,
     DEPRECATED_FEATURE_TEST(ShouldCacheFirstCreatedTracerUsingStrongReference)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.opencensus
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  auto message_one = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  std::weak_ptr<Tracing::Driver> tracer_one = factory.createTracerDriver(*message_one, context);
  // Verify that tracer factory keeps a strong reference.
  EXPECT_NE(nullptr, tracer_one.lock());

  auto message_two = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer_two = factory.createTracerDriver(*message_two, context);
  EXPECT_NE(nullptr, tracer_two);
  // Verify that no new tracer has been created.
  EXPECT_EQ(tracer_two, tracer_one.lock());
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(ShouldNotCacheInvalidConfiguration)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_one = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      ocagent_exporter_enabled: true
      ocagent_grpc_service:
        envoy_grpc:
          cluster_name: opencensus
  )EOF";
  envoy::config::trace::v3::Tracing configuration_one;
  TestUtility::loadFromYaml(yaml_one, configuration_one);

  auto message_one = Config::Utility::translateToFactoryConfig(
      configuration_one.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_MESSAGE((factory.createTracerDriver(*message_one, context)), EnvoyException,
                            "Opencensus ocagent tracer only supports GoogleGrpc.");

  const std::string yaml_two = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      ocagent_exporter_enabled: true
      ocagent_grpc_service:
        google_grpc:
          target_uri: 127.0.0.1:55678
          stat_prefix: test
  )EOF";
  envoy::config::trace::v3::Tracing configuration_two;
  TestUtility::loadFromYaml(yaml_two, configuration_two);

  auto message_two = Config::Utility::translateToFactoryConfig(
      configuration_two.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
#ifdef ENVOY_GOOGLE_GRPC
  auto tracer_two = factory.createTracerDriver(*message_two, context);
  // Verify that a new tracer has been created despite an earlier failed attempt.
  EXPECT_NE(nullptr, tracer_two);
#else
  EXPECT_THROW_WITH_MESSAGE(
      (factory.createTracerDriver(*message_two, context)), EnvoyException,
      "Opencensus tracer: cannot handle ocagent google grpc service, google grpc is not built in.");
#endif
}

TEST(OpenCensusTracerConfigTest,
     DEPRECATED_FEATURE_TEST(ShouldRejectSubsequentCreateAttemptsWithDifferentConfig)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  OpenCensusTracerFactory factory;

  const std::string yaml_one = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 123
  )EOF";
  envoy::config::trace::v3::Tracing configuration_one;
  TestUtility::loadFromYaml(yaml_one, configuration_one);

  auto message_one = Config::Utility::translateToFactoryConfig(
      configuration_one.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer_one = factory.createTracerDriver(*message_one, context);
  EXPECT_NE(nullptr, tracer_one);

  const std::string yaml_two = R"EOF(
  http:
    name: envoy.tracers.opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      trace_config:
        rate_limiting_sampler:
          qps: 321
  )EOF";
  envoy::config::trace::v3::Tracing configuration_two;
  TestUtility::loadFromYaml(yaml_two, configuration_two);

  auto message_two = Config::Utility::translateToFactoryConfig(
      configuration_two.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  // Verify that OpenCensus is only configured once in a lifetime.
  EXPECT_THROW_WITH_MESSAGE((factory.createTracerDriver(*message_two, context)), EnvoyException,
                            "Opencensus has already been configured with a different config.");
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(OpenCensusHttpTracerStackdriverGrpc)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      stackdriver_exporter_enabled: true
      stackdriver_grpc_service:
        google_grpc:
          target_uri: 127.0.0.1:55678
          stat_prefix: test
        initial_metadata:
        - key: foo
          value: bar
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
#ifdef ENVOY_GOOGLE_GRPC
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);
#else
  EXPECT_THROW_WITH_MESSAGE((factory.createTracerDriver(*message, context)), EnvoyException,
                            "Opencensus tracer: cannot handle stackdriver google grpc service, "
                            "google grpc is not built in.");
#endif
}

TEST(OpenCensusTracerConfigTest, DEPRECATED_FEATURE_TEST(OpenCensusHttpTracerStackdriverAddress)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  const std::string yaml_string = R"EOF(
  http:
    name: opencensus
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenCensusConfig
      stackdriver_exporter_enabled: true
      stackdriver_address: 127.0.0.1:55678
  )EOF";

  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  OpenCensusTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
