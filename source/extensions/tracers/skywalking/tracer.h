#pragma once

#include <memory>

#include "envoy/tracing/trace_driver.h"

#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/skywalking/trace_segment_reporter.h"

#include "cpp2sky/tracing_context.h"
#include "cpp2sky/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::TracingContextPtr;
using cpp2sky::TracingSpanPtr;

const Http::LowerCaseString& skywalkingPropagationHeaderKey();

class Tracer {
public:
  Tracer(TraceSegmentReporterPtr reporter);

  /*
   * Report trace segment data to backend tracing service.
   *
   * @param segment_context The segment context.
   */
  void sendSegment(TracingContextPtr tracing_context);

  /*
   * Create a new span based on the segment context and parent span.
   *
   * @param config The tracing config.
   * @param start_time Start time of span.
   * @param operation Operation name of span.
   * @param segment_context The SkyWalking segment context. The newly created span belongs to this
   * segment.
   * @param parent The parent span pointer. If parent is null, then the newly created span is first
   * span of this segment.
   *
   * @return The unique ptr to the newly created span.
   */
  Tracing::SpanPtr startSpan(const Tracing::Config& config, SystemTime start_time,
                             const std::string& operation, TracingContextPtr tracing_context,
                             TracingSpanPtr parent);

private:
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

class Span : public Tracing::Span {
public:
  Span(TracingSpanPtr span_entity, TracingContextPtr tracing_context, Tracer& parent_tracer)
      : parent_tracer_(parent_tracer), span_entity_(span_entity),
        tracing_context_(tracing_context) {}

  // Tracing::Span
  void setOperation(absl::string_view) override {}
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool do_sample) override;
  std::string getBaggage(absl::string_view) override { return EMPTY_STRING; }
  void setBaggage(absl::string_view, absl::string_view) override {}
  std::string getTraceIdAsHex() const override { return EMPTY_STRING; }

  const TracingContextPtr tracingContext() { return tracing_context_; }
  const TracingSpanPtr spanEntity() { return span_entity_; }

private:
  Tracer& parent_tracer_;
  TracingSpanPtr span_entity_;
  TracingContextPtr tracing_context_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
