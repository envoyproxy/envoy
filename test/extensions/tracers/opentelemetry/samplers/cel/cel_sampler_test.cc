#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.pb.h"

#include "source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class CELSamplerTest : public testing::Test {
public:
  CELSamplerTest() {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.cel
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.CELSamplerConfig
      expression:
        cel_expr_parsed:
          expr:
            id: 4
            call_expr:
              function: _==_
              args:
              - id: 3
                select_expr:
                  field: id
                  operand:
                    id: 2
                    select_expr:
                      field: node
                      operand:
                        id: 1
                        ident_expr:
                          name: xds
              - id: 5
                const_expr:
                  string_value: "node_name"
  )EOF";
    TestUtility::loadFromYaml(yaml, typed_config);
    auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
        "envoy.tracers.opentelemetry.samplers.cel");
    sampler_ = factory->createSampler(typed_config.typed_config(), context_);
  }

protected:
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  SamplerSharedPtr sampler_;
};

// Verify sampler being invoked with an invalid span context
TEST_F(CELSamplerTest, TestWithInvalidParentContext) {
  EXPECT_STREQ(sampler_->getDescription().c_str(), "CELSampler");

  auto sampling_result =
      sampler_->shouldSample(stream_info_, absl::nullopt, "operation_name", "12345",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with a valid span context
TEST_F(CELSamplerTest, TestWithValidParentContext) {
  EXPECT_STREQ(sampler_->getDescription().c_str(), "CELSampler");

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  SpanContext span_context("0", "12345", "45678", false, "some_tracestate");
  auto sampling_result = sampler_->shouldSample(
      stream_info_, span_context, "operation_name", "12345",
      ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, request_headers, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "some_tracestate");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

TEST_F(CELSamplerTest, TestEval) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.cel
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.CELSamplerConfig
      expression:
        cel_expr_parsed:
          expr:
            id: 3
            call_expr:
              function: _==_
              args:
              - id: 2
                select_expr:
                  operand:
                    id: 1
                    ident_expr:
                      name: request
                  field: path
              - id: 4
                const_expr:
                  string_value: "/test-1234-deny"
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
      "envoy.tracers.opentelemetry.samplers.cel");
  sampler_ = factory->createSampler(typed_config.typed_config(), context_);

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  SpanContext span_context("0", "12345", "45678", false, "some_tracestate");
  auto sampling_result = sampler_->shouldSample(
      stream_info_, span_context, "operation_name", "12345",
      ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, request_headers, {});
  EXPECT_EQ(sampling_result.decision, Decision::Drop);
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "");
  EXPECT_FALSE(sampling_result.isRecording());
  EXPECT_FALSE(sampling_result.isSampled());
}

TEST_F(CELSamplerTest, TestDecisionDrop) {
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.cel
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.CELSamplerConfig
      expression:
        cel_expr_parsed:
          expr:
            id: 4
            call_expr:
              function: _!=_
              args:
              - id: 3
                select_expr:
                  field: id
                  operand:
                    id: 2
                    select_expr:
                      field: node
                      operand:
                        id: 1
                        ident_expr:
                          name: xds
              - id: 5
                const_expr:
                  string_value: "node_name"
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
      "envoy.tracers.opentelemetry.samplers.cel");
  sampler_ = factory->createSampler(typed_config.typed_config(), context_);

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  SpanContext span_context("0", "12345", "45678", false, "some_tracestate");
  auto sampling_result = sampler_->shouldSample(
      stream_info_, span_context, "operation_name", "12345",
      ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, request_headers, {});
  EXPECT_EQ(sampling_result.decision, Decision::Drop);
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "");
  EXPECT_FALSE(sampling_result.isRecording());
  EXPECT_FALSE(sampling_result.isSampled());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
