#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/parent_based_sampler.pb.h"
#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"

#include "source/common/common/random_generator.h"
#include "source/extensions/tracers/opentelemetry/samplers/parent_based/parent_based_sampler.h"
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

// // Verify the ShouldSample function
TEST(ParentBasedSamplerTest, TestShouldSample) {
  // Case 1: Parent doesn't exist -> Return result of delegateSampler()
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
      trace_id_ratio_based_config;
  envoy::extensions::tracers::opentelemetry::samplers::v3::ParentBasedSamplerConfig
      parent_based_config;
  std::srand(std::time(nullptr));

  for (int i = 0; i < 100; ++i) {
    uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                           percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_denominator(
        percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_numerator(numerator);
    auto wrapped_sampler =
        std::make_shared<TraceIdRatioBasedSampler>(trace_id_ratio_based_config, context);

    auto sampler =
        std::make_shared<ParentBasedSampler>(parent_based_config, context, wrapped_sampler);

    Random::RandomGeneratorImpl random_generator;
    uint64_t trace_id_high = random_generator.random();
    uint64_t trace_id_low = random_generator.random();
    auto trace_id = absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id_low));

    auto root_sampling_result = wrapped_sampler->shouldSample(
        info, absl::nullopt, trace_id, "a_random_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
    auto sampling_result =
        sampler->shouldSample(info, absl::nullopt, trace_id, "a_random_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

    EXPECT_EQ(sampling_result.decision, root_sampling_result.decision);
    EXPECT_EQ(sampling_result.isRecording(), root_sampling_result.isRecording());
    EXPECT_EQ(sampling_result.isSampled(), root_sampling_result.isSampled());
  }

  // Case 2: Parent exists and SampledFlag is true -> Return RecordAndSample.
  for (int i = 0; i < 10; ++i) {
    uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                           percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_denominator(
        percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_numerator(numerator);
    auto wrapped_sampler =
        std::make_shared<TraceIdRatioBasedSampler>(trace_id_ratio_based_config, context);

    auto sampler =
        std::make_shared<ParentBasedSampler>(parent_based_config, context, wrapped_sampler);

    Random::RandomGeneratorImpl random_generator;
    uint64_t trace_id_high = random_generator.random();
    uint64_t trace_id_low = random_generator.random();
    auto trace_id = absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id_low));
    SpanContext parent_context("0", "12345", "45678", true, "random_key=random_value");
    auto sampling_result =
        sampler->shouldSample(info, parent_context, trace_id, "a_random_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

    EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
    EXPECT_TRUE(sampling_result.isRecording());
    EXPECT_TRUE(sampling_result.isSampled());
  }

  // Case 3: Parent exists and SampledFlag is false -> Return Drop.
  for (int i = 0; i < 10; ++i) {
    uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                           percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_denominator(
        percentage_denominator);
    trace_id_ratio_based_config.mutable_sampling_percentage()->set_numerator(numerator);
    auto wrapped_sampler =
        std::make_shared<TraceIdRatioBasedSampler>(trace_id_ratio_based_config, context);

    auto sampler =
        std::make_shared<ParentBasedSampler>(parent_based_config, context, wrapped_sampler);

    Random::RandomGeneratorImpl random_generator;
    uint64_t trace_id_high = random_generator.random();
    uint64_t trace_id_low = random_generator.random();
    auto trace_id = absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id_low));
    SpanContext parent_context("0", "12345", "45678", false, "random_key=random_value");
    auto sampling_result =
        sampler->shouldSample(info, parent_context, trace_id, "a_random_name",
                              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

    EXPECT_EQ(sampling_result.decision, Decision::Drop);
    EXPECT_FALSE(sampling_result.isRecording());
    EXPECT_FALSE(sampling_result.isSampled());
  }
}

// Verify the GetDescription function and other tracing attributes
TEST(ParentBasedSamplerTest, TestGetDescriptionAndContext) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
      trace_id_ratio_based_config;
  std::srand(std::time(nullptr));
  uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                         percentage_denominator);
  trace_id_ratio_based_config.mutable_sampling_percentage()->set_denominator(
      percentage_denominator);
  trace_id_ratio_based_config.mutable_sampling_percentage()->set_numerator(numerator);
  auto wrapped_sampler =
      std::make_shared<TraceIdRatioBasedSampler>(trace_id_ratio_based_config, context);

  envoy::extensions::tracers::opentelemetry::samplers::v3::ParentBasedSamplerConfig
      parent_based_config;
  auto sampler =
      std::make_shared<ParentBasedSampler>(parent_based_config, context, wrapped_sampler);

  SpanContext parent_context("0", "12345", "45678", false, "random_key=random_value");
  auto sampling_result = sampler->shouldSample(
      info, parent_context, "3f4a7c912a5a82779d2a7573f4c758ba", "a_random_name",
      ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_STREQ(sampler->getDescription().c_str(), "ParentBasedSampler");
  EXPECT_EQ(sampling_result.attributes, nullptr);
  EXPECT_STREQ(sampling_result.tracestate.c_str(), "random_key=random_value");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
