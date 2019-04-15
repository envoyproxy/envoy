// Usage:
// bazel run //test/extensions/tracers/opencensus:tracer_test -- -l debug

#include <iostream>
#include <vector>

#include "envoy/config/trace/v2/trace.pb.validate.h"

#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/trace/exporter/span_data.h"
#include "opencensus/trace/exporter/span_exporter.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_id.h"

using testing::NiceMock;
using testing::Return;

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

using envoy::config::trace::v2::OpenCensusConfig;
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
  std::vector<SpanData> spans_ GUARDED_BY(mu_);
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
  std::unique_ptr<Tracing::Driver> driver(new OpenCensus::Driver(oc_config));

  NiceMock<Tracing::MockConfig> config;
  Http::TestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const std::string operation_name{"my_operation_1"};
  SystemTime start_time;

  {
    Tracing::SpanPtr span = driver->startSpan(config, request_headers, operation_name, start_time,
                                              {Tracing::Reason::Sampling, true});
    span->setOperation("my_operation_2");
    span->setTag("my_key", "my_value");
    span->log(start_time, "my annotation");
    // TODO: injectContext.
    // TODO: spawnChild.
    span->setSampled(false); // Abandon tracer.
    span->finishSpan();
  }

  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  ASSERT_EQ(1, spans.size());
  const auto& sd = spans[0];
  ENVOY_LOG_MISC(debug, "{}", sd.DebugString());

  EXPECT_EQ("my_operation_1", sd.name());
  EXPECT_TRUE(sd.context().IsValid());
  EXPECT_TRUE(sd.context().trace_options().IsSampled());
  ::opencensus::trace::SpanId zeros;
  EXPECT_EQ(zeros, sd.parent_span_id());

  ASSERT_EQ(3, sd.annotations().events().size());
  EXPECT_EQ("setOperation", sd.annotations().events()[0].event().description());
  EXPECT_EQ("my annotation", sd.annotations().events()[1].event().description());
  EXPECT_EQ("setSampled", sd.annotations().events()[2].event().description());

  EXPECT_TRUE(sd.has_ended());
}

// Test that trace context propagation works.
TEST(OpenCensusTracerTest, PropagateTraceContext) {
  registerSpanCatcher();
  OpenCensusConfig oc_config;
  oc_config.add_incoming_trace_context(OpenCensusConfig::trace_context);
  std::unique_ptr<Tracing::Driver> driver(new OpenCensus::Driver(oc_config));

  NiceMock<Tracing::MockConfig> config;
  Http::TestHeaderMapImpl request_headers{
      {":path", "/"},
      {":method", "GET"},
      {"x-request-id", "foo"},
      {"traceparent", "00-404142434445464748494a4b4c4d4e4f-6162636465666768-01"},
  };
  const std::string operation_name{"my_operation_2"};
  SystemTime start_time;

  {
    Tracing::SpanPtr span = driver->startSpan(config, request_headers, operation_name, start_time,
                                              {Tracing::Reason::Sampling, false});
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
}

// Test constant_sampler that's always on.
TEST(OpenCensusTracerTest, ConstantSamplerAlwaysOn) {
  registerSpanCatcher();
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_constant_sampler()->set_decision(
      ::opencensus::proto::trace::v1::ConstantSampler::ALWAYS_ON);
  std::unique_ptr<Tracing::Driver> driver(new OpenCensus::Driver(oc_config));
  auto span = ::opencensus::trace::Span::StartSpan("span_on");
  span.End();
  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  ASSERT_EQ(1, spans.size());
  EXPECT_TRUE(spans[0].context().trace_options().IsSampled());
}

// Test constant_sampler that's always off.
TEST(OpenCensusTracerTest, ConstantSamplerAlwaysOff) {
  registerSpanCatcher();
  OpenCensusConfig oc_config;
  oc_config.mutable_trace_config()->mutable_constant_sampler()->set_decision(
      ::opencensus::proto::trace::v1::ConstantSampler::ALWAYS_OFF);
  std::unique_ptr<Tracing::Driver> driver(new OpenCensus::Driver(oc_config));
  auto span = ::opencensus::trace::Span::StartSpan("span_off");
  span.End();
  // Retrieve SpanData from the OpenCensus trace exporter.
  std::vector<SpanData> spans = getSpanCatcher()->catchSpans();
  EXPECT_EQ(0, spans.size());
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
