#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.h"

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

const char* trace_id = "67a9a23155e1741b5b35368e08e6ece5";

const char* parent_span_id = "9d83def9a4939b7b";

const char* dt_tracestate_ignored =
    "5b3f9fed-980df25c@dt=fw4;4;4af38366;0;0;1;2;123;8eae;2h01;3h4af38366;4h00;5h01;"
    "6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b";
const char* dt_tracestate_sampled =
    "5b3f9fed-980df25c@dt=fw4;4;4af38366;0;0;0;0;123;8eae;2h01;3h4af38366;4h00;5h01;"
    "6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b";
const char* dt_tracestate_ignored_different_tenant =
    "6666ad40-980df25c@dt=fw4;4;4af38366;0;0;1;2;123;8eae;2h01;3h4af38366;4h00;5h01;"
    "6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b";

} // namespace

class MockSamplerConfigProvider : public SamplerConfigProvider {
public:
  MOCK_METHOD(const SamplerConfig&, getSamplerConfig, (), (const override));
};

class DynatraceSamplerTestBase {
public:
  DynatraceSamplerTestBase() {
    TestUtility::loadFromYaml(yaml_string_, proto_config_);
    auto scf = std::make_unique<NiceMock<MockSamplerConfigProvider>>();
    ON_CALL(*scf, getSamplerConfig()).WillByDefault(testing::ReturnRef(sampler_config_));

    timer_ = new NiceMock<Event::MockTimer>(
        &tracer_factory_context_.server_factory_context_.dispatcher_);
    ON_CALL(tracer_factory_context_.server_factory_context_.dispatcher_, createTimer_(_))
        .WillByDefault(Invoke([this](Event::TimerCb) { return timer_; }));
    sampler_ =
        std::make_unique<DynatraceSampler>(proto_config_, tracer_factory_context_, std::move(scf));
  }

protected:
  const std::string yaml_string_ = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
  )EOF";
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> tracer_factory_context_;
  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config_;
  SamplerConfig sampler_config_{SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT};
  NiceMock<Event::MockTimer>* timer_;
  std::unique_ptr<DynatraceSampler> sampler_;
};

class DynatraceSamplerTest : public DynatraceSamplerTestBase, public testing::Test {};

// Verify getDescription
TEST_F(DynatraceSamplerTest, TestGetDescription) {
  EXPECT_STREQ(sampler_->getDescription().c_str(), "DynatraceSampler");
}

// Verify sampler being invoked with an invalid/empty span context
TEST_F(DynatraceSamplerTest, TestWithoutParentContext) {
  auto sampling_result =
      sampler_->shouldSample(stream_info_, absl::nullopt, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes->size(), 2);
  EXPECT_EQ(opentelemetry::nostd::get<uint32_t>(
                sampling_result.attributes->find("supportability.atm_sampling_ratio")->second),
            1);

  auto tcr = sampling_result.attributes->find("trace.capture.reasons");
  ASSERT_NE(tcr, sampling_result.attributes->end());
  auto tcr_values = opentelemetry::nostd::get<std::vector<absl::string_view>>(tcr->second);
  ASSERT_EQ(tcr_values.size(), 1);
  EXPECT_EQ(tcr_values[0], "atm");

  EXPECT_STREQ(sampling_result.tracestate.c_str(),
               "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked without a Dynatrace tracestate
TEST_F(DynatraceSamplerTest, TestWithUnknownParentContext) {
  SpanContext parent_context("00", trace_id, parent_span_id, true, "some_vendor=some_value");

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes->size(), 2);
  EXPECT_EQ(opentelemetry::nostd::get<uint32_t>(
                sampling_result.attributes->find("supportability.atm_sampling_ratio")->second),
            1);
  // Dynatrace tracestate should be prepended
  EXPECT_STREQ(sampling_result.tracestate.c_str(),
               "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,some_vendor=some_value");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with Dynatrace trace state
TEST_F(DynatraceSamplerTest, TestWithDynatraceParentContextSampled) {
  SpanContext parent_context("00", trace_id, parent_span_id, true, dt_tracestate_sampled);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes->size(), 1);
  EXPECT_EQ(opentelemetry::nostd::get<uint32_t>(
                sampling_result.attributes->find("supportability.atm_sampling_ratio")->second),
            1);
  // tracestate should be forwarded
  EXPECT_STREQ(sampling_result.tracestate.c_str(), dt_tracestate_sampled);
  // sampling decision from parent should be respected
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with an invalid Dynatrace trace state
TEST_F(DynatraceSamplerTest, TestWithInvalidDynatraceParentContext) {
  const char* invalidts = "5b3f9fed-980df25c@dt=fw4;4";
  SpanContext parent_context("00", trace_id, parent_span_id, true, invalidts);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_STREQ(sampling_result.tracestate.c_str(),
               "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,5b3f9fed-980df25c@dt=fw4;4");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with an invalid Dynatrace trace state
TEST_F(DynatraceSamplerTest, TestWithInvalidDynatraceParentContext1) {
  // invalid tracestate[6] has to be an int
  const char* invalidts = "5b3f9fed-980df25c@dt=fw4;4;4af38366;0;0;0;X;123";
  SpanContext parent_context("00", trace_id, parent_span_id, true, invalidts);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_STREQ(
      sampling_result.tracestate.c_str(),
      "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,5b3f9fed-980df25c@dt=fw4;4;4af38366;"
      "0;0;0;X;123");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with an old Dynatrace trace state version
TEST_F(DynatraceSamplerTest, TestWithDynatraceParentContextOtherVersion) {
  const char* oldts =
      "5b3f9fed-980df25c@dt=fw3;4;4af38366;0;0;0;0;123;8eae;2h01;3h4af38366;4h00;5h01;"
      "6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b";
  SpanContext parent_context("00", trace_id, parent_span_id, true, oldts);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_STREQ(
      sampling_result.tracestate.c_str(),
      "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,5b3f9fed-980df25c@dt=fw3;4;4af38366;0;0;"
      "0;0;"
      "123;"
      "8eae;2h01;3h4af38366;4h00;5h01;6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b");
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being invoked with Dynatrace trace parent where ignored flag is set
TEST_F(DynatraceSamplerTest, TestWithDynatraceParentContextIgnored) {
  SpanContext parent_context("00", trace_id, parent_span_id, true, dt_tracestate_ignored);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  EXPECT_EQ(sampling_result.decision, Decision::Drop);
  EXPECT_EQ(sampling_result.attributes->size(), 2);
  EXPECT_EQ(opentelemetry::nostd::get<uint32_t>(
                sampling_result.attributes->find("supportability.atm_sampling_ratio")->second),
            4);
  EXPECT_EQ(opentelemetry::nostd::get<uint64_t>(
                sampling_result.attributes->find("sampling.threshold")->second),
            54043195528445952);

  // tracestate should be forwarded
  EXPECT_STREQ(sampling_result.tracestate.c_str(), dt_tracestate_ignored);
  // sampling decision from parent should be respected
  EXPECT_FALSE(sampling_result.isRecording());
  EXPECT_FALSE(sampling_result.isSampled());
}

// Verify sampler being invoked with Dynatrace trace parent from a different tenant
TEST_F(DynatraceSamplerTest, TestWithDynatraceParentContextFromDifferentTenant) {
  SpanContext parent_context("00", trace_id, parent_span_id, true,
                             dt_tracestate_ignored_different_tenant);

  auto sampling_result =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});
  // sampling decision on tracestate should be ignored because it is from a different tenant.
  EXPECT_EQ(sampling_result.decision, Decision::RecordAndSample);
  EXPECT_EQ(sampling_result.attributes->size(), 2);
  EXPECT_EQ(opentelemetry::nostd::get<uint32_t>(
                sampling_result.attributes->find("supportability.atm_sampling_ratio")->second),
            1);
  // new Dynatrace tag should be prepended, already existing tag should be kept
  const char* exptected =
      "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,6666ad40-980df25c@dt=fw4;4;4af38366;0;0;"
      "1;2;"
      "123;"
      "8eae;2h01;3h4af38366;4h00;5h01;6h67a9a23155e1741b5b35368e08e6ece5;7h9d83def9a4939b7b";
  EXPECT_STREQ(sampling_result.tracestate.c_str(), exptected);
  EXPECT_TRUE(sampling_result.isRecording());
  EXPECT_TRUE(sampling_result.isSampled());
}

// Verify sampler being called during warm up phase (no recent top_k available)
TEST_F(DynatraceSamplerTest, TestWarmup) {
  // config should allow 200 root spans per minute
  sampler_config_.parse("{\n \"rootSpansPerMinute\" : 200 \n }");

  Tracing::TestTraceContextImpl trace_context_1{};
  trace_context_1.context_method_ = "GET";
  trace_context_1.context_path_ = "/path";

  // timer is not invoked, because we want to test warm up phase.
  // we use 200 as threshold. As long as number of requests is < (threshold/2), exponent should be 0
  uint32_t ignored = 0;
  uint32_t sampled = 0;
  for (int i = 0; i < 99; i++) {
    auto result = sampler_->shouldSample(
        stream_info_, {}, std::to_string(1000 + i), "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, trace_context_1, {});
    result.isSampled() ? sampled++ : ignored++;
  }
  EXPECT_EQ(ignored, 0);
  EXPECT_EQ(sampled, 99);

  // next (threshold/2) spans will get exponent 1, every second span will be sampled
  for (int i = 0; i < 100; i++) {
    auto result = sampler_->shouldSample(
        stream_info_, {}, std::to_string(1000 + i), "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, trace_context_1, {});
    result.isSampled() ? sampled++ : ignored++;
  }
  // should be 50 ignored, but the used "random" in shouldSample does not produce the same odd/even
  // numbers.
  EXPECT_EQ(ignored, 41);
  EXPECT_EQ(sampled, 158);

  // send more requests
  for (int i = 0; i < 100; i++) {
    auto result = sampler_->shouldSample(
        stream_info_, {}, std::to_string(1000 + i), "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, trace_context_1, {});
    result.isSampled() ? sampled++ : ignored++;
  }
  // exponent should be 2, with a perfect random we would get 25 sampled and 75 ignored.
  EXPECT_EQ(ignored, 113);
  EXPECT_EQ(sampled, 186);

  // send more requests.
  for (int i = 0; i < 700; i++) {
    auto result = sampler_->shouldSample(
        stream_info_, {}, std::to_string(1000 + i), "operation_name",
        ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, trace_context_1, {});
    result.isSampled() ? sampled++ : ignored++;
  }
  // with a perfect random, the number of sampled paths would be lower than threshold (200)
  // We don't care about exceeding the threshold because it is not a hard limit
  EXPECT_EQ(ignored, 791);
  EXPECT_EQ(sampled, 208);
}

// Verify sampling if number of configured spans per minute is exceeded.
TEST_F(DynatraceSamplerTest, TestSampling) {
  // config should allow 200 root spans per minute
  sampler_config_.parse("{\n \"rootSpansPerMinute\" : 200 \n }");

  Tracing::TestTraceContextImpl trace_context_1{};
  trace_context_1.context_method_ = "GET";
  trace_context_1.context_path_ = "/path";
  Tracing::TestTraceContextImpl trace_context_2{};
  trace_context_2.context_method_ = "POST";
  trace_context_2.context_path_ = "/path";
  Tracing::TestTraceContextImpl trace_context_3{};
  trace_context_3.context_method_ = "POST";
  trace_context_3.context_path_ = "/another_path";

  // simulate requests
  for (int i = 0; i < 180; i++) {
    sampler_->shouldSample(stream_info_, {}, trace_id, "operation_name",
                           ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER,
                           trace_context_1, {});
    sampler_->shouldSample(stream_info_, {}, trace_id, "operation_name",
                           ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER,
                           trace_context_2, {});
  }

  sampler_->shouldSample(stream_info_, {}, trace_id, "operation_name",
                         ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, trace_context_3,
                         {});

  // sampler should update sampling exponents based on number of requests in the previous period
  timer_->invokeCallback();

  // the sampler should not sample every span for 'trace_context_1'
  // we call it again 10 times. This should be enough to get at least one ignored span
  // 'i' is used as 'random trace_id'
  bool ignored = false;
  for (int i = 0; i < 10; i++) {
    auto result = sampler_->shouldSample(stream_info_, {}, std::to_string(i), "operation_name",
                                         ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER,
                                         trace_context_1, {});
    if (!result.isSampled()) {
      ignored = true;
      break;
    }
  }
  EXPECT_TRUE(ignored);

  // trace_context_3 should always be sampled.
  for (int i = 0; i < 10; i++) {
    auto result = sampler_->shouldSample(stream_info_, {}, std::to_string(i), "operation_name",
                                         ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER,
                                         trace_context_2, {});
    EXPECT_TRUE(result.isSampled());
  }
}

struct SamplingResultTestData {
  std::vector<std::string> tcr_values;
  std::string tracestate;

  SamplingResultTestData(std::vector<std::string> tcr, std::string t)
      : tcr_values(std::move(tcr)), tracestate(std::move(t)) {}
};

class DynatraceSamplerTraceCaptureReasonTest
    : public DynatraceSamplerTestBase,
      public ::testing::TestWithParam<std::tuple<std::string, SamplingResultTestData>> {};

// Verify sampler behavior depending on which trace capture reason was received
TEST_P(DynatraceSamplerTraceCaptureReasonTest, TraceCaptureReasonScenarios) {
  std::string incomingTraceState = std::get<0>(GetParam());
  SamplingResultTestData expected = std::get<1>(GetParam());

  SpanContext parent_context("00", trace_id, parent_span_id, true, incomingTraceState);

  auto actual =
      sampler_->shouldSample(stream_info_, parent_context, trace_id, "operation_name",
                             ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER, {}, {});

  // Check that the sampling result contains the expected trace capture reason attributes
  if (!expected.tcr_values.empty()) {
    auto actual_tcr_values = opentelemetry::nostd::get<std::vector<absl::string_view>>(
        actual.attributes->find("trace.capture.reasons")->second);
    ASSERT_EQ(actual_tcr_values.size(), expected.tcr_values.size());

    std::vector<std::string> actual_sorted;
    actual_sorted.reserve(actual_tcr_values.size());
    for (const auto& v : actual_tcr_values) {
      actual_sorted.push_back(std::string(v));
    }

    std::sort(actual_sorted.begin(), actual_sorted.end());
    std::sort(expected.tcr_values.begin(), expected.tcr_values.end());
    EXPECT_EQ(actual_sorted, expected.tcr_values);
  } else {
    // verify that there's no trace.capture.reasons attribute in the map
    EXPECT_EQ(actual.attributes->find("trace.capture.reasons"), actual.attributes->end());
  }

  EXPECT_STREQ(actual.tracestate.c_str(), expected.tracestate.c_str());
}

INSTANTIATE_TEST_SUITE_P(
    TraceCaptureReasonTestCase, DynatraceSamplerTraceCaptureReasonTest,
    // tracestate with Dynatrace tag but no trace capture reason extension
    ::testing::Values(
        // No trace capture reason present in the tracestate
        std::make_tuple(
            "5b3f9fed-980df25c@dt=fw4;4;4af38366;0;0;1;2;123;8eae;2h01;3h4af38366;4h00;5h01;",
            SamplingResultTestData(
                {},
                "5b3f9fed-980df25c@dt=fw4;4;4af38366;0;0;1;2;123;8eae;2h01;3h4af38366;4h00;5h01;")),

        // Valid trace capture reason present in the tracestate
        std::make_tuple(
            "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101",
            SamplingResultTestData({"atm"}, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101")),

        // trace capture reason present in the tracestate with an unsupported version
        std::make_tuple(
            "5b3f9fed-980df25c@dt=fw3;0;0;0;0;0;0;95;8h0101",
            SamplingResultTestData({"atm"}, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101,"
                                            "5b3f9fed-980df25c@dt=fw3;0;0;0;0;0;0;95;8h0101")),

        // Multiple, valid trace capture reasons present in the tracestate
        std::make_tuple("5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;8h0107",
                        SamplingResultTestData({"atm", "fixed", "custom"},
                                               "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;8h0107")),

        // trace state with multiple extensions, but no trace capture reason
        std::make_tuple(
            "5b3f9fed-980df25c@dt=fw4;4;de84a412;50a92;0;0;0;13d;6edd;2h02;3he2a8e619;4h075f04;"
            "5h01;6h1f8d0931f1bbe07139169c26faddb564;7hcc46d06657b9b021",
            SamplingResultTestData(
                {}, "5b3f9fed-980df25c@dt=fw4;4;de84a412;50a92;0;0;0;13d;6edd;2h02;3he2a8e619;"
                    "4h075f04;5h01;6h1f8d0931f1bbe07139169c26faddb564;7hcc46d06657b9b021")),

        // Root trace started by Envoy - sampler should use the correct reason
        std::make_tuple("", SamplingResultTestData(
                                {"atm"}, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;95;3b1a;8h0101"))));
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
