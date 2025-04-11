#include <cstdint>
#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"

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

// As per the docs: https://opentelemetry.io/docs/specs/otel/trace/sdk/#traceidratiobased
// > A TraceIDRatioBased sampler with a given sampling rate MUST also sample
//	 all traces that any TraceIDRatioBased sampler with a lower sampling rate
//	 would sample.
TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRatioSamplesInclusively) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;

  std::srand(std::time(nullptr));
  for (int i = 0; i < 100; ++i) {
    double ratio_low = static_cast<double>(std::rand()) / RAND_MAX;
    double ratio_high = static_cast<double>(std::rand()) / RAND_MAX;
    if (ratio_low > ratio_high) {
      double holder = ratio_low;
      ratio_low = ratio_high;
      ratio_high = holder;
    }
    envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
        config_low;
    envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig
        config_high;
    config_low.set_ratio(ratio_low);
    config_high.set_ratio(ratio_high);
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
  config.set_ratio(0);
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

  // ratio < 0, should never sample
  config.set_ratio(-5);
  sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);

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
  config.set_ratio(1);
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

  // ratio < 0, should never sample
  config.set_ratio(7);
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
  config.set_ratio(0.0157);
  auto sampler = std::make_shared<TraceIdRatioBasedSampler>(config, context);
  EXPECT_STREQ(sampler->getDescription().c_str(), "TraceIdRatioBasedSampler{0.015700}");
}

TEST(TraceIdRatioBasedSamplerTest, TestTraceIdRatioAttrs) {
  envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig config;
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::srand(std::time(nullptr));
  config.set_ratio(static_cast<double>(std::rand()) / RAND_MAX);
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
