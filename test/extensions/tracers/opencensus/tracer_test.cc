// Usage:
// bazel run //test/extensions/tracers/opencensus:tracer_test -- -l debug

#include <cstdint>
#include <iostream>
#include <vector>

#include "envoy/config/trace/v3/opencensus.pb.h"

#include "common/common/base64.h"

#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/trace/exporter/span_data.h"
#include "opencensus/trace/exporter/span_exporter.h"
#include "opencensus/trace/propagation/b3.h"
#include "opencensus/trace/propagation/cloud_trace_context.h"
#include "opencensus/trace/propagation/grpc_trace_bin.h"
#include "opencensus/trace/propagation/trace_context.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_id.h"

using testing::NiceMock;

namespace opencensus {
namespace trace {
namespace exporter {

class SpanExporterTestPeer {
public:
  static constexpr auto& exportForTesting = SpanExporter::ExportForTesting;
};

} // namespace exporter
} // namespace trace
} // namespace opencensus

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

using envoy::config::trace::v3::OpenCensusConfig;
using ::opencensus::trace::exporter::SpanData;
using ::opencensus::trace::exporter::SpanExporter;

namespace {

// Custom export handler. We register this as an OpenCensus trace exporter, and
// use it to catch the spans we produce.
class SpanCatcher : public SpanExporter::Handler {
public:
  void Export(const std::vector<SpanData>& spans) override {
    absl::MutexLock lock(&mu_);
    for (const auto& span : spans) {
      spans_.emplace_back(span);
    }
  }

  // Returns generated SpanData, and clears the catcher.
  std::vector<SpanData> catchSpans() {
    // OpenCensus's trace exporter is running in a background thread, waiting
    // for a periodic export. Force it to flush right now.
    opencensus::trace::exporter::SpanExporterTestPeer::exportForTesting();
    absl::MutexLock lock(&mu_);
    std::vector<SpanData> ret = std::move(spans_);
    spans_.clear();
    return ret;
  }

private:
  mutable absl::Mutex mu_;
  std::vector<SpanData> spans_ ABSL_GUARDED_BY(mu_);
};

// Use a Singleton SpanCatcher.
SpanCatcher* getSpanCatcher() {
  static auto g_span_catcher = new SpanCatcher();
  return g_span_catcher;
}

// Call this before generating spans to register the exporter that catches them.
void registerSpanCatcher() {
  static bool done_once = false;
  if (done_once) {
    return;
  }
  SpanExporter::RegisterHandler(absl::WrapUnique(getSpanCatcher()));
  done_once = true;
}

} // namespace

// Create a Span via the driver, test all of the Tracing::Span API, and verify
// the produced SpanData.
TEST(OpenCensusTracerTest, Span) {
  registerSpanCatcher();
  OpenCensusConfig oc_config;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  std::unique_ptr<Tracing::Driver> driver(
      new OpenCensus::Driver(oc_config, local_info, *Api::createApiForTest()));

  NiceMock<Tracing::MockConfig> config;
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const std::string operation_name{"my_operation_1"};
  SystemTime start_time;

  {
    Tracing::SpanPtr span = driver->startSpan(config, request_headers, operation_name, start_time,
                                              {Tracing::Reason::Sampling, true});
    span->setOperation("different_name");
    span->setTag("my_key", "my_value");
    span->log(start_time, "my annotation");
    // injectContext is tested in another unit test.
    Tracing::SpanPtr child = span->spawnChild(config, "child_span", start_time);
    child->finishSpan();
    span->setSampled(false); // Abandon tracer.
    span->finishSpan();
  }

  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  ASSERT_EQ(2, spans.size());
  ::opencensus::trace::SpanId parent_span_id;

  // Check contents of parent span.
  {
    const auto& sd = (spans[0].name() == operation_name) ? spans[0] : spans[1];
    ENVOY_LOG_MISC(debug, "{}", sd.DebugString());

    EXPECT_EQ("different_name", sd.name());
    EXPECT_TRUE(sd.context().IsValid());
    EXPECT_TRUE(sd.context().trace_options().IsSampled());
    ::opencensus::trace::SpanId zeros;
    EXPECT_EQ(zeros, sd.parent_span_id());
    parent_span_id = sd.context().span_id();

    ASSERT_EQ(3, sd.annotations().events().size());
    EXPECT_EQ("my annotation", sd.annotations().events()[0].event().description());
    EXPECT_EQ("spawnChild", sd.annotations().events()[1].event().description());
    EXPECT_EQ("setSampled", sd.annotations().events()[2].event().description());
    EXPECT_TRUE(sd.has_ended());
  }

  // And child span.
  {
    const auto& sd = (spans[0].name() == "child_span") ? spans[0] : spans[1];
    ENVOY_LOG_MISC(debug, "{}", sd.DebugString());

    EXPECT_EQ("child_span", sd.name());
    EXPECT_TRUE(sd.context().IsValid());
    EXPECT_TRUE(sd.context().trace_options().IsSampled());
    EXPECT_EQ(parent_span_id, sd.parent_span_id());
    EXPECT_TRUE(sd.has_ended());
  }
}

namespace {

using testing::PrintToString;

MATCHER_P2(ContainHeader, header, expected_value,
           "contains the header " + PrintToString(header) + " with value " +
               PrintToString(expected_value)) {
  const auto found_value = arg.get(Http::LowerCaseString(header));
  if (found_value == nullptr) {
    return false;
  }
  return found_value->value().getStringView() == expected_value;
}

// Given incoming headers, test that trace context propagation works and generates all the expected
// outgoing headers.
void testIncomingHeaders(
    const std::initializer_list<std::pair<const char*, const char*>>& headers) {
  registerSpanCatcher();
  OpenCensusConfig oc_config;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  oc_config.add_incoming_trace_context(OpenCensusConfig::NONE);
  oc_config.add_incoming_trace_context(OpenCensusConfig::B3);
  oc_config.add_incoming_trace_context(OpenCensusConfig::TRACE_CONTEXT);
  oc_config.add_incoming_trace_context(OpenCensusConfig::GRPC_TRACE_BIN);
  oc_config.add_incoming_trace_context(OpenCensusConfig::CLOUD_TRACE_CONTEXT);
  oc_config.add_outgoing_trace_context(OpenCensusConfig::NONE);
  oc_config.add_outgoing_trace_context(OpenCensusConfig::B3);
  oc_config.add_outgoing_trace_context(OpenCensusConfig::TRACE_CONTEXT);
  oc_config.add_outgoing_trace_context(OpenCensusConfig::GRPC_TRACE_BIN);
  oc_config.add_outgoing_trace_context(OpenCensusConfig::CLOUD_TRACE_CONTEXT);
  std::unique_ptr<Tracing::Driver> driver(
      new OpenCensus::Driver(oc_config, local_info, *Api::createApiForTest()));
  NiceMock<Tracing::MockConfig> config;
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"},
      {":method", "GET"},
      {"x-request-id", "foo"},
  };
  for (const auto& kv : headers) {
    request_headers.addCopy(Http::LowerCaseString(kv.first), kv.second);
  }

  const std::string operation_name{"my_operation_2"};
  SystemTime start_time;
  Http::TestRequestHeaderMapImpl injected_headers;
  {
    Tracing::SpanPtr span = driver->startSpan(config, request_headers, operation_name, start_time,
                                              {Tracing::Reason::Sampling, false});
    span->injectContext(injected_headers);
    span->finishSpan();
  }

  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  ASSERT_EQ(1, spans.size());
  const auto& sd = spans[0];
  ENVOY_LOG_MISC(debug, "{}", sd.DebugString());

  // Check contents.
  EXPECT_TRUE(sd.has_remote_parent());
  EXPECT_EQ("6162636465666768", sd.parent_span_id().ToHex());
  EXPECT_EQ("404142434445464748494a4b4c4d4e4f", sd.context().trace_id().ToHex());
  EXPECT_TRUE(sd.context().trace_options().IsSampled())
      << "parent was sampled, child should be also";

  // Check injectContext.
  // The SpanID is unpredictable so re-serialize context to check it.
  const auto& ctx = sd.context();
  const auto& hdrs = injected_headers;
  EXPECT_THAT(hdrs, ContainHeader("traceparent",
                                  ::opencensus::trace::propagation::ToTraceParentHeader(ctx)));
  {
    std::string expected = ::opencensus::trace::propagation::ToGrpcTraceBinHeader(ctx);
    expected = Base64::encode(expected.data(), expected.size(), /*add_padding=*/false);
    EXPECT_THAT(hdrs, ContainHeader("grpc-trace-bin", expected));
  }
  EXPECT_THAT(hdrs,
              ContainHeader("x-cloud-trace-context",
                            ::opencensus::trace::propagation::ToCloudTraceContextHeader(ctx)));
  EXPECT_THAT(hdrs, ContainHeader("x-b3-traceid", "404142434445464748494a4b4c4d4e4f"));
  EXPECT_THAT(
      hdrs, ContainHeader("x-b3-spanid", ::opencensus::trace::propagation::ToB3SpanIdHeader(ctx)));
  EXPECT_THAT(hdrs, ContainHeader("x-b3-sampled", "1"));
}
} // namespace

TEST(OpenCensusTracerTest, PropagateTraceParentContext) {
  testIncomingHeaders({{"traceparent", "00-404142434445464748494a4b4c4d4e4f-6162636465666768-01"}});
}

TEST(OpenCensusTracerTest, PropagateGrpcTraceBinContext) {
  testIncomingHeaders({{"grpc-trace-bin", "AABAQUJDREVGR0hJSktMTU5PAWFiY2RlZmdoAgE"}});
}

TEST(OpenCensusTracerTest, PropagateCloudTraceContext) {
  testIncomingHeaders(
      {{"x-cloud-trace-context", "404142434445464748494a4b4c4d4e4f/7017280452245743464;o=1"}});
}

TEST(OpenCensusTracerTest, PropagateB3Context) {
  testIncomingHeaders({{"x-b3-traceid", "404142434445464748494a4b4c4d4e4f"},
                       {"x-b3-spanid", "6162636465666768"},
                       {"x-b3-sampled", "1"}});
}

TEST(OpenCensusTracerTest, PropagateB3ContextWithDebugFlag) {
  testIncomingHeaders({{"x-b3-traceid", "404142434445464748494a4b4c4d4e4f"},
                       {"x-b3-spanid", "6162636465666768"},
                       {"x-b3-flags", "1"}}); // Debug flag causes sampling.
}

namespace {

// Create a Span using the given config and return how many spans made it to
// the exporter (either zero or one).
int SamplerTestHelper(const OpenCensusConfig& oc_config) {
  registerSpanCatcher();
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  std::unique_ptr<Tracing::Driver> driver(
      new OpenCensus::Driver(oc_config, local_info, *Api::createApiForTest()));
  auto span = ::opencensus::trace::Span::StartSpan("test_span");
  span.End();
  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  EXPECT_GE(spans.size(), 0);
  EXPECT_LE(spans.size(), 1);
  if (!spans.empty()) {
    EXPECT_TRUE(spans[0].context().trace_options().IsSampled());
  }
  return spans.size();
}

} // namespace

// Test constant_sampler that's always on.
TEST(OpenCensusTracerTest, ConstantSamplerAlwaysOn) {
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_constant_sampler()->set_decision(
      ::opencensus::proto::trace::v1::ConstantSampler::ALWAYS_ON);
  EXPECT_EQ(1, SamplerTestHelper(oc_config));
}

// Test constant_sampler that's always off.
TEST(OpenCensusTracerTest, ConstantSamplerAlwaysOff) {
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_constant_sampler()->set_decision(
      ::opencensus::proto::trace::v1::ConstantSampler::ALWAYS_OFF);
  EXPECT_EQ(0, SamplerTestHelper(oc_config));
}

// Test probability_sampler that's always on.
TEST(OpenCensusTracerTest, ProbabilitySamplerAlwaysOn) {
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_probability_sampler()->set_samplingprobability(1.0);
  EXPECT_EQ(1, SamplerTestHelper(oc_config));
}

// Test probability_sampler that's always off.
TEST(OpenCensusTracerTest, ProbabilitySamplerAlwaysOff) {
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_probability_sampler()->set_samplingprobability(0.0);
  EXPECT_EQ(0, SamplerTestHelper(oc_config));
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
