
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using ::testing::NiceMock;
using ::testing::StrictMock;

class TestSampler : public Sampler {
public:
  MOCK_METHOD(SamplingResult, shouldSample,
              ((absl::StatusOr<SpanContext>&), (const std::string&), (const std::string&),
               (::opentelemetry::proto::trace::v1::Span::SpanKind),
               (const std::map<std::string, std::string>&), (const std::set<SpanContext>)),
              (override));
  MOCK_METHOD(std::string, getDescription, (), (const, override));
  MOCK_METHOD(std::string, modifyTracestate,
              (const std::string& span_id, const std::string& current_tracestate), (const));
};

class TestSamplerFactory : public SamplerFactory {
public:
  MOCK_METHOD(SamplerPtr, createSampler,
              (const Protobuf::Message& message,
               Server::Configuration::TracerFactoryContext& context));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return "envoy.tracers.opentelemetry.samplers.testsampler"; }
};

class SamplerFactoryTest : public testing::Test {

protected:
  NiceMock<Tracing::MockConfig> config;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
};

TEST_F(SamplerFactoryTest, TestWithoutSampler) {
  // using StrictMock, calls to SamplerFactory would cause a test failure
  auto test_sampler = std::make_shared<StrictMock<TestSampler>>();
  StrictMock<TestSamplerFactory> sampler_factory;
  Registry::InjectFactory<SamplerFactory> sampler_factory_registration(sampler_factory);

  // no sampler configured
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  auto driver = std::make_unique<Driver>(opentelemetry_config, context);

  driver->startSpan(config, trace_context, stream_info, "operation_name",
                    {Tracing::Reason::Sampling, true});
}

TEST_F(SamplerFactoryTest, TestWithInvalidSampler) {
  // using StrictMock, calls to SamplerFactory would cause a test failure
  auto test_sampler = std::make_shared<StrictMock<TestSampler>>();
  StrictMock<TestSamplerFactory> sampler_factory;
  Registry::InjectFactory<SamplerFactory> sampler_factory_registration(sampler_factory);

  // invalid sampler configured
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    sampler:
      name: envoy.tracers.opentelemetry.samplers.testsampler
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Value
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  EXPECT_THROW(std::make_unique<Driver>(opentelemetry_config, context), EnvoyException);
}

TEST_F(SamplerFactoryTest, TestWithSampler) {
  auto test_sampler = std::make_shared<NiceMock<TestSampler>>();
  TestSamplerFactory sampler_factory;
  Registry::InjectFactory<SamplerFactory> sampler_factory_registration(sampler_factory);

  EXPECT_CALL(sampler_factory, createSampler(_, _)).WillOnce(Return(test_sampler));

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    sampler:
      name: envoy.tracers.opentelemetry.samplers.testsampler
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  auto driver = std::make_unique<Driver>(opentelemetry_config, context);

  EXPECT_CALL(*test_sampler, modifyTracestate(_, _));
  EXPECT_CALL(*test_sampler, shouldSample(_, _, _, _, _, _));
  driver->startSpan(config, trace_context, stream_info, "operation_name",
                    {Tracing::Reason::Sampling, true});
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
