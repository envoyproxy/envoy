#include <cstdint>
#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/random_generator.h"
#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

const auto percentage_denominator = envoy::type::v3::FractionalPercent::MILLION;

// As per the docs: https://opentelemetry.io/docs/specs/otel/trace/sdk/#traceidratiobased
// > A TraceIDRatioBased sampler with a given sampling rate MUST also sample
//	 all traces that any TraceIDRatioBased sampler with a lower sampling rate
//	 would sample.
TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRatioSamplesInclusively) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;

  std::srand(std::time(nullptr));
  for (int i = 0; i < 100; ++i) {
    uint64_t numerator_low = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                               percentage_denominator);
    uint64_t numerator_high =
        std::rand() %
        ProtobufPercentHelper::fractionalPercentDenominatorToInt(percentage_denominator);
    if (numerator_low > numerator_high) {
      double holder = numerator_low;
      numerator_low = numerator_high;
      numerator_high = holder;
    }
    envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
        config_low;
    envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
        config_high;
    config_low.mutable_sampling_percentage()->set_denominator(percentage_denominator);
    config_low.mutable_sampling_percentage()->set_numerator(numerator_low);
    config_high.mutable_sampling_percentage()->set_denominator(percentage_denominator);
    config_high.mutable_sampling_percentage()->set_numerator(numerator_high);
    auto sampler_low = std::make_shared<TraceIdRatioBasedSampler>(config_low, context);
    auto sampler_high = std::make_shared<TraceIdRatioBasedSampler>(config_high, context);

    Random::RandomGeneratorImpl random_generator;
    auto trace_id = absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                                 Hex::uint64ToHex(random_generator.random()));

    auto sampling_result_low = sampler_low->shouldSample(
        info, absl::nullopt, trace_id, "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

    if (sampling_result_low.decision == Decision::RecordAndSample) {
      EXPECT_TRUE(sampling_result_low.isSampled());
      EXPECT_TRUE(sampling_result_low.isRecording());

      auto sampling_result_high = sampler_high->shouldSample(
          info, absl::nullopt, trace_id, "operation_name",
          ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
      EXPECT_TRUE(sampling_result_high.isRecording());
      EXPECT_TRUE(sampling_result_high.isSampled());
    }
  }
}

// Test special ratios including 0, 1, and numbers out of [0, 1]
TEST(TraceIdRatioBasedSamplerTest, TestSpecialRatios) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  std::srand(std::time(nullptr));

  // ratio = 0, should never sample
  config.mutable_sampling_percentage()->set_denominator(percentage_denominator);
  config.mutable_sampling_percentage()->set_numerator(0);
  auto sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);

  for (int i = 0; i < 10; ++i) {
    Random::RandomGeneratorImpl random_generator;
    auto trace_id = absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                                 Hex::uint64ToHex(random_generator.random()));
    auto sampling_result =
        sampler->shouldSample(info, absl::nullopt, trace_id, "operation_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
    EXPECT_EQ(sampling_result.decision, Decision::Drop);
  }

  // ratio = 1, should always sample
  config.mutable_sampling_percentage()->set_numerator(
      ProtobufPercentHelper::fractionalPercentDenominatorToInt(percentage_denominator));
  sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);

  for (int i = 0; i < 10; ++i) {
    Random::RandomGeneratorImpl random_generator;
    auto trace_id = absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                                 Hex::uint64ToHex(random_generator.random()));
    auto sampling_result =
        sampler->shouldSample(info, absl::nullopt, trace_id, "operation_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
    EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  }

  // ratio > 1, should always sample
  config.mutable_sampling_percentage()->set_numerator(
      7 * ProtobufPercentHelper::fractionalPercentDenominatorToInt(percentage_denominator));
  sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);

  for (int i = 0; i < 10; ++i) {
    Random::RandomGeneratorImpl random_generator;
    auto trace_id = absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                                 Hex::uint64ToHex(random_generator.random()));
    auto sampling_result =
        sampler->shouldSample(info, absl::nullopt, trace_id, "operation_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
    EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  }
}

TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRatioDescription) {
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  config.mutable_sampling_percentage()->set_denominator(percentage_denominator);
  config.mutable_sampling_percentage()->set_numerator(157);
  auto sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);
  EXPECT_STREQ(sampler->getDescription().c_str(), "TraceIdRatioBasedSampler{157/1000000}");
}

TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRatioAttrs) {
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::srand(std::time(nullptr));
  uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                         percentage_denominator);
  config.mutable_sampling_percentage()->set_denominator(percentage_denominator);
  config.mutable_sampling_percentage()->set_numerator(numerator);
  auto sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);
  SpanContext parent_context("0", "12345", "45678", true, "random_key=random_value");
  auto sampling_result = sampler->shouldSample(
      info, parent_context, "8a23f4f4e6efde581e64092eebc3a682", "operation_name",
      ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "random_key=random_value");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
