#pragma once

#include <memory>

#include "envoy/tracing/trace_driver.h"

#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/skywalking/trace_segment_reporter.h"

#include "cpp2sky/tracing_context.h"
#include "cpp2sky/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::TracingContextSharedPtr;
using cpp2sky::TracingSpanSharedPtr;

const Tracing::TraceContextHandler& skywalkingPropagationHeaderKey();

class Tracer {
public:
  Tracer(TraceSegmentReporterPtr reporter);

  /*
   * Report trace segment data to backend tracing service.
   *
   * @param segment_context The segment context.
   */
  void sendSegment(TracingContextSharedPtr tracing_context);

  /*
   * Create a new span based on the segment context and parent span.
   *
   * @param name Operation name of span.
   * @param tracing_context The SkyWalking tracing context. The newly created span belongs to this
   * context.
   *
   * @return The unique ptr to the newly created span.
   */
  Tracing::SpanPtr startSpan(absl::string_view name, absl::string_view protocol,
                             TracingContextSharedPtr tracing_context);

private:
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

class Span : public Tracing::Span {
public:
  Span(absl::string_view name, absl::string_view protocol, TracingContextSharedPtr tracing_context,
       Tracer& parent_tracer)
      : parent_tracer_(parent_tracer), tracing_context_(tracing_context),
        span_entity_(tracing_context_->createEntrySpan()) {
    span_entity_->startSpan({name.data(), name.size()});
    skywalking::v3::SpanLayer layer;
    if (absl::StrContains(protocol, "HTTP")) {
      // TraceContext.protocol of http is parsed from http message, which value could be HTTP/1.1,
      // etc.
      layer = skywalking::v3::SpanLayer::Http;
    } else if (!skywalking::v3::SpanLayer_Parse(std::string(protocol), &layer)) {
      layer = skywalking::v3::SpanLayer::Unknown;
    }
    span_entity_->setSpanLayer(layer);
  }
  Span(absl::string_view name, skywalking::v3::SpanLayer span_layer, Span& parent_span,
       TracingContextSharedPtr tracing_context, Tracer& parent_tracer)
      : parent_tracer_(parent_tracer), tracing_context_(tracing_context),
        span_entity_(tracing_context_->createExitSpan(parent_span.spanEntity())) {
    span_entity_->startSpan({name.data(), name.size()});
    span_entity_->setSpanLayer(span_layer);
  }

  // Tracing::Span
  void setOperation(absl::string_view) override {}
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext& upstream) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool do_sample) override;
  std::string getBaggage(absl::string_view) override { return EMPTY_STRING; }
  void setBaggage(absl::string_view, absl::string_view) override {}
  std::string getTraceId() const override { return tracing_context_->traceId(); }
  std::string getSpanId() const override { return EMPTY_STRING; }

  const TracingContextSharedPtr tracingContext() { return tracing_context_; }
  const TracingSpanSharedPtr spanEntity() { return span_entity_; }

private:
  Tracer& parent_tracer_;
  TracingContextSharedPtr tracing_context_;
  TracingSpanSharedPtr span_entity_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
