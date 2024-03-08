#include <algorithm>

#include "envoy/common/optref.h"
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
              ((const absl::optional<SpanContext>), (const std::string&), (const std::string&),
               (OTelSpanKind), (OptRef<const Tracing::TraceContext>),
               (const std::vector<SpanContext>&)),
              (override));
  MOCK_METHOD(std::string, getDescription, (), (const, override));
};

class TestSamplerFactory : public SamplerFactory {
public:
  MOCK_METHOD(SamplerSharedPtr, createSampler,
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

// Test tracer category()
TEST_F(SamplerFactoryTest, TestGetName) {
  TestSamplerFactory factory;
  EXPECT_STREQ(factory.category().c_str(), "envoy.tracers.opentelemetry.samplers");
}

// Test OTLP tracer without a sampler
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

// Test config containing an unknown sampler
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

// Test OTLP tracer with a sampler
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

  // shouldSample returns a result without additional attributes and Decision::RecordAndSample
  EXPECT_CALL(*test_sampler, shouldSample(_, _, _, _, _, _))
      .WillOnce([](const absl::optional<SpanContext>, const std::string&, const std::string&,
                   OTelSpanKind, OptRef<const Tracing::TraceContext>,
                   const std::vector<SpanContext>&) {
        SamplingResult res;
        res.decision = Decision::RecordAndSample;
        res.tracestate = "this_is=tracesate";
        return res;
      });

  Tracing::SpanPtr tracing_span = driver->startSpan(
      config, trace_context, stream_info, "operation_name", {Tracing::Reason::Sampling, true});
  // startSpan returns a Tracing::SpanPtr. Tracing::Span has no sampled() method.
  // We know that the underlying span is Extensions::Tracers::OpenTelemetry::Span
  // So the dynamic_cast should be safe.
  std::unique_ptr<Span> span(dynamic_cast<Span*>(tracing_span.release()));
  EXPECT_TRUE(span->sampled());
  EXPECT_STREQ(span->tracestate().c_str(), "this_is=tracesate");

  // shouldSamples return a result containing additional attributes and Decision::Drop
  EXPECT_CALL(*test_sampler, shouldSample(_, _, _, _, _, _))
      .WillOnce([](const absl::optional<SpanContext>, const std::string&, const std::string&,
                   OTelSpanKind, OptRef<const Tracing::TraceContext>,
                   const std::vector<SpanContext>&) {
        SamplingResult res;
        res.decision = Decision::Drop;
        OtelAttributes attributes;
        attributes["char_key"] = "char_value";
        attributes["sv_key"] = absl::string_view("sv_value");
        attributes["bool_key"] = true;
        attributes["int_key"] = static_cast<int32_t>(123);
        attributes["uint_key"] = static_cast<uint32_t>(123);
        attributes["int64_t_key"] = static_cast<int64_t>(INT64_MAX);
        attributes["uint64_t_key"] = static_cast<uint64_t>(UINT64_MAX);
        attributes["double_key"] = 0.123;
        attributes["not_supported_span"] = opentelemetry::nostd::span<bool>();

        res.attributes = std::make_unique<const OtelAttributes>(std::move(attributes));
        res.tracestate = "this_is=another_tracesate";
        return res;
      });
  tracing_span = driver->startSpan(config, trace_context, stream_info, "operation_name",
                                   {Tracing::Reason::Sampling, true});
  std::unique_ptr<Span> unsampled_span(dynamic_cast<Span*>(tracing_span.release()));
  EXPECT_FALSE(unsampled_span->sampled());
  EXPECT_STREQ(unsampled_span->tracestate().c_str(), "this_is=another_tracesate");
  auto proto_span = unsampled_span->spanForTest();

  auto get_attr_value =
      [&proto_span](const char* name) -> ::opentelemetry::proto::common::v1::AnyValue* {
    for (auto& key_value : *proto_span.mutable_attributes()) {
      if (key_value.key() == name) {
        return key_value.mutable_value();
      }
    }
    return nullptr;
  };

  ASSERT_NE(get_attr_value("char_key"), nullptr);
  EXPECT_STREQ(get_attr_value("char_key")->string_value().c_str(), "char_value");

  ASSERT_NE(get_attr_value("sv_key"), nullptr);
  EXPECT_STREQ(get_attr_value("sv_key")->string_value().c_str(), "sv_value");

  ASSERT_NE(get_attr_value("bool_key"), nullptr);
  EXPECT_EQ(get_attr_value("bool_key")->bool_value(), true);

  ASSERT_NE(get_attr_value("int_key"), nullptr);
  EXPECT_EQ(get_attr_value("int_key")->int_value(), 123);

  ASSERT_NE(get_attr_value("uint_key"), nullptr);
  EXPECT_EQ(get_attr_value("uint_key")->int_value(), 123);

  ASSERT_NE(get_attr_value("int64_t_key"), nullptr);
  EXPECT_EQ(get_attr_value("int64_t_key")->int_value(), INT64_MAX);

  ASSERT_NE(get_attr_value("uint64_t_key"), nullptr);
  EXPECT_EQ(get_attr_value("uint64_t_key")->int_value(), UINT64_MAX);

  ASSERT_NE(get_attr_value("double_key"), nullptr);
  EXPECT_EQ(get_attr_value("double_key")->double_value(), 0.123);
}

// Test that sampler receives trace_context
TEST_F(SamplerFactoryTest, TestInitialAttributes) {
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

  auto expected = makeOptRef<const Tracing::TraceContext>(trace_context);
  EXPECT_CALL(*test_sampler, shouldSample(_, _, _, _, expected, _));
  driver->startSpan(config, trace_context, stream_info, "operation_name",
                    {Tracing::Reason::Sampling, true});
}

// Test sampling result decision
TEST(SamplingResultTest, TestSamplingResult) {
  SamplingResult result;
  result.decision = Decision::RecordAndSample;
  EXPECT_TRUE(result.isRecording());
  EXPECT_TRUE(result.isSampled());
  result.decision = Decision::RecordOnly;
  EXPECT_TRUE(result.isRecording());
  EXPECT_FALSE(result.isSampled());
  result.decision = Decision::Drop;
  EXPECT_FALSE(result.isRecording());
  EXPECT_FALSE(result.isSampled());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
