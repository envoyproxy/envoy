#pragma once

#include <iostream>
#include <memory>

#include "envoy/common/pure.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/skywalking_types.h"
#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Span;

class Tracer {
public:
  explicit Tracer(TimeSource& time_source) : time_source_(time_source) {}
  virtual ~Tracer() { reporter_->closeStream(); }

  /*
   * Set a trace segment reporter to the current Tracer. Whenever a SkyWalking segment ends, the
   * reporter will be used to report segment data.
   *
   * @param reporter The unique ptr of trace segment reporter.
   */
  void setReporter(TraceSegmentReporterPtr&& reporter) { reporter_ = std::move(reporter); }

  /*
   * Report trace segment data to backend tracing service.
   *
   * @param segment_context The segment context.
   */
  void report(const SegmentContext& segment_context) { return reporter_->report(segment_context); }

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
                             const std::string& operation, SegmentContextSharedPtr segment_context,
                             Span* parent);

  TimeSource& time_source_;

private:
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

class Span : public Tracing::Span {
public:
  /*
   * Constructor of span.
   *
   * @param segment_context The SkyWalking segment context.
   * @param span_store Pointer to a SpanStore object. Whenever a new span is created, a new
   * SpanStore object is created and stored in the segment context. This parameter can never be
   * null.
   * @param tracer Reference to tracer.
   */
  Span(SegmentContextSharedPtr segment_context, SpanStore* span_store, Tracer& tracer)
      : segment_context_(std::move(segment_context)), span_store_(span_store), tracer_(tracer) {}

  // Tracing::Span
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;
  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;

  // TODO: This method is unimplemented for OpenTracing.
  std::string getTraceId() const override { return std::string(); };

  /*
   * Get pointer to corresponding SpanStore object. This method is mainly used in testing. Used to
   * check the internal data of the span.
   */
  SpanStore* spanStore() const { return span_store_; }
  SegmentContext* segmentContext() const { return segment_context_.get(); }

private:
  void tryToReportSpan() {
    // If the current span is the root span of the entire segment and its sampling flag is not
    // false, the data for the entire segment is reported. Please ensure that the root span is the
    // last span to end in the entire segment.
    if (span_store_->sampled() && span_store_->spanId() == 0) {
      tracer_.report(*segment_context_);
    }
  }

  SegmentContextSharedPtr segment_context_;
  SpanStore* span_store_;

  Tracer& tracer_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
