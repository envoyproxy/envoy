#pragma once

#include <memory>

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/trace_segment_reporter.h"

#include "cpp2sky/segment_context.h"
#include "cpp2sky/tracer.h"
#include "cpp2sky/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::CurrentSegmentSpanPtr;
using cpp2sky::SegmentContextPtr;
using SkywalkingTracer = cpp2sky::Tracer;

const Http::LowerCaseString& skywalkingPropagationHeaderKey();

class Span : public Tracing::Span {
public:
  Span(CurrentSegmentSpanPtr span_entity, SegmentContextPtr segment_context,
       SkywalkingTracer& parent_tracer)
      : parent_tracer_(parent_tracer), span_entity_(span_entity),
        segment_context_(segment_context) {}

  // Tracing::Span
  void setOperation(absl::string_view) override {}
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestam, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool do_sample) override;
  std::string getBaggage(absl::string_view) override { return EMPTY_STRING; }
  void setBaggage(absl::string_view, absl::string_view) override {}
  std::string getTraceIdAsHex() const override { return EMPTY_STRING; }

  const SegmentContextPtr segmentContext() { return segment_context_; }
  const CurrentSegmentSpanPtr spanEntity() { return span_entity_; }

private:
  SkywalkingTracer& parent_tracer_;
  CurrentSegmentSpanPtr span_entity_;
  SegmentContextPtr segment_context_;
};

class Tracer : public SkywalkingTracer {
public:
  Tracer(TraceSegmentReporterPtr reporter);

  /*
   * Report trace segment data to backend tracing service.
   *
   * @param segment_context The segment context.
   */
  void sendSegment(SegmentContextPtr segment_context) override;

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
                             const std::string& operation, SegmentContextPtr segment_context,
                             CurrentSegmentSpanPtr parent);

private:
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
