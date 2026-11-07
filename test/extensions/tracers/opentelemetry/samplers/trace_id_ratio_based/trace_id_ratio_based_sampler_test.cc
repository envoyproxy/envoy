#include <cstdint>
#include <random>
#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/byte_order.h"
#include "source/common/common/random_generator.h"
#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

const auto percentage_denominator = envoy::type::v3::FractionalPercent::MILLION;

TEST(TraceIdRatioBasedSamplerTest, TestTraceIdToUint64) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  config.mutable_sampling_percentage()->set_denominator(percentage_denominator);
  uint64_t numerator = std::rand() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                         percentage_denominator);
  config.mutable_sampling_percentage()->set_numerator(numerator);
  auto sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);

  // Test with an empty string.
  std::string empty_trace_id = "";
  EXPECT_EQ(sampler->traceIdToUint64(empty_trace_id), 0);

  // Test with a string smaller than 16 characters.
  std::string short_trace_id = "5b8aa5a";
  EXPECT_EQ(sampler->traceIdToUint64(short_trace_id), 0);

  uint64_t first_8_bytes = fromEndianness<ByteOrder::BigEndian>(0x5b8aa5a2d2c872e8ULL);

  // Test with a string of exactly 16 characters.
  std::string trace_id_16 = "5b8aa5a2d2c872e8";
  EXPECT_EQ(sampler->traceIdToUint64(trace_id_16), first_8_bytes);

  // Test with a string larger than 16 characters.
  std::string long_trace_id = "5b8aa5a2d2c872e8321cf37308d69df2";
  EXPECT_EQ(sampler->traceIdToUint64(long_trace_id), first_8_bytes);
}

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
        info, std::nullopt, trace_id, "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

    if (sampling_result_low.decision == Decision::RecordAndSample) {
      EXPECT_TRUE(sampling_result_low.isSampled());
      EXPECT_TRUE(sampling_result_low.isRecording());

      auto sampling_result_high = sampler_high->shouldSample(
          info, std::nullopt, trace_id, "operation_name",
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
        sampler->shouldSample(info, std::nullopt, trace_id, "operation_name",
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
        sampler->shouldSample(info, std::nullopt, trace_id, "operation_name",
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
        sampler->shouldSample(info, std::nullopt, trace_id, "operation_name",
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

namespace {

std::shared_ptr<TraceIdRatioBasedSampler>
makeSampler(uint32_t numerator, envoy::type::v3::FractionalPercent::DenominatorType denominator,
            Server::Configuration::TracerFactoryContext& context) {
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  config.mutable_sampling_percentage()->set_numerator(numerator);
  config.mutable_sampling_percentage()->set_denominator(denominator);
  return std::make_shared<TraceIdRatioBasedSampler>(config, context);
}

Decision sample(TraceIdRatioBasedSampler& sampler, const StreamInfo::StreamInfo& info,
                const std::string& trace_id) {
  return sampler
      .shouldSample(info, std::nullopt, trace_id, "operation_name",
                    ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {})
      .decision;
}

// Counts sampled traces among trace ids whose high 32 bits are a fixed Unix timestamp in seconds
// and whose other 96 bits are random.
uint64_t countSampledWithTimestampPrefix(TraceIdRatioBasedSampler& sampler, uint64_t trials) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::mt19937_64 rng(42);
  const uint64_t timestamp_prefix = uint64_t{0x6a0b1c2d} << 32;
  uint64_t sampled = 0;
  for (uint64_t i = 0; i < trials; ++i) {
    const uint64_t high = timestamp_prefix | (rng() & 0xffffffff);
    const std::string trace_id = absl::StrCat(Hex::uint64ToHex(high), Hex::uint64ToHex(rng()));
    if (sample(sampler, info, trace_id) == Decision::RecordAndSample) {
      ++sampled;
    }
  }
  return sampled;
}

} // namespace

TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRandomness) {
  EXPECT_EQ(TraceIdRatioBasedSampler::traceIdRandomness("0af7651916cd43dd8448eb211c80319c"),
            0x48eb211c80319cULL);
  EXPECT_EQ(TraceIdRatioBasedSampler::traceIdRandomness("000000000000000000ffffffffffffff"),
            (uint64_t{1} << 56) - 1);
  EXPECT_ENVOY_BUG(TraceIdRatioBasedSampler::traceIdRandomness("5b8aa5a2d2c872e8"),
                   "trace id is not 32 hex characters");
  EXPECT_ENVOY_BUG(TraceIdRatioBasedSampler::traceIdRandomness("0af7651916cd43dd8448eb211c80319z"),
                   "trace id is not 32 hex characters");
  EXPECT_ENVOY_BUG(TraceIdRatioBasedSampler::traceIdRandomness("0af7651916cd43dd84 0x2211c80319c"),
                   "trace id is not 32 hex characters");
}

TEST(TraceIdRatioBasedSamplerTest, TestRejectionThreshold) {
  envoy::type::v3::FractionalPercent percentage;
  percentage.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);

  percentage.set_numerator(0);
  EXPECT_EQ(TraceIdRatioBasedSampler::rejectionThreshold(percentage), uint64_t{1} << 56);
  percentage.set_numerator(50);
  EXPECT_EQ(TraceIdRatioBasedSampler::rejectionThreshold(percentage), uint64_t{1} << 55);
  percentage.set_numerator(100);
  EXPECT_EQ(TraceIdRatioBasedSampler::rejectionThreshold(percentage), 0);
  percentage.set_numerator(700);
  EXPECT_EQ(TraceIdRatioBasedSampler::rejectionThreshold(percentage), 0);

  percentage.set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  percentage.set_numerator(1);
  EXPECT_EQ(TraceIdRatioBasedSampler::rejectionThreshold(percentage),
            (uint64_t{1} << 56) - (uint64_t{1} << 56) / 1000000);
}

// The decision is R >= T, the rule of the OpenTelemetry ProbabilitySampler.
TEST(TraceIdRatioBasedSamplerTest, TestSamplesAtAndAboveThreshold) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  auto sampler = makeSampler(50, envoy::type::v3::FractionalPercent::HUNDRED, context);

  EXPECT_EQ(sample(*sampler, info, "ffffffffffffffffff7fffffffffffff"), Decision::Drop);
  EXPECT_EQ(sample(*sampler, info, "00000000000000000080000000000000"), Decision::RecordAndSample);
  EXPECT_EQ(sample(*sampler, info, "000000000000000000ffffffffffffff"), Decision::RecordAndSample);
}

TEST(TraceIdRatioBasedSamplerTest, TestRatioHoldsWithTimestampPrefixedTraceIds) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  constexpr uint64_t trials = 20000;

  auto one_percent = makeSampler(1, envoy::type::v3::FractionalPercent::HUNDRED, context);
  const uint64_t sampled_one_percent = countSampledWithTimestampPrefix(*one_percent, trials);
  EXPECT_GT(sampled_one_percent, 140);
  EXPECT_LT(sampled_one_percent, 260);

  auto ten_percent = makeSampler(10, envoy::type::v3::FractionalPercent::HUNDRED, context);
  const uint64_t sampled_ten_percent = countSampledWithTimestampPrefix(*ten_percent, trials);
  EXPECT_GT(sampled_ten_percent, 1800);
  EXPECT_LT(sampled_ten_percent, 2200);
}

TEST(TraceIdRatioBasedSamplerTest, TestRuntimeGuardDisabledReadsFirstEightBytes) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.otel_ratio_sampler_use_trace_id_rightmost_bytes", "false"}});
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  auto sampler = makeSampler(50, envoy::type::v3::FractionalPercent::HUNDRED, context);

  // The first 8 bytes read the same in either byte order: 0 % 100 < 50 and 0x0101010101010101 %
  // 100 = 73.
  EXPECT_EQ(sample(*sampler, info, "00000000000000000000000000000001"), Decision::RecordAndSample);
  EXPECT_EQ(sample(*sampler, info, "010101010101010100ffffffffffffff"), Decision::Drop);
}

TEST(TraceIdRatioBasedSamplerTest, TestRuntimeGuardEnabledReadsRightmostBytes) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  auto sampler = makeSampler(50, envoy::type::v3::FractionalPercent::HUNDRED, context);

  // T = 2^55.
  EXPECT_EQ(sample(*sampler, info, "00000000000000000000000000000001"), Decision::Drop);
  EXPECT_EQ(sample(*sampler, info, "010101010101010100ffffffffffffff"), Decision::RecordAndSample);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
