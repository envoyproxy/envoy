#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/tracing/http_tracer.h"

#include "extensions/tracers/xray/sampling_strategy.h"
#include "extensions/tracers/xray/xray_configuration.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

static const char XRayTraceHeader[] = "x-amzn-trace-id";

class Span : public Tracing::Span {
public:
  /**
   * Construct a new X-Ray Span.
   */
  Span(TimeSource& time_source) : time_source_(time_source) {}

  /**
   * Set the Span's trace ID.
   */
  void setTraceId(absl::string_view trace_id) { trace_id_ = std::string(trace_id); };

  /**
   * Get the Span's trace ID.
   */
  const std::string& traceId() const { return trace_id_; }

  /**
   * Complete the current span, serialize it and send it to the X-Ray daemon.
   */
  void finishSpan() override;

  /**
   * Set the current operation name on the Span.
   * This information will be included in the X-Ray span's metadata.
   */
  void setOperation(absl::string_view operation) override {
    operation_name_ = std::string(operation);
  }

  /**
   * Set the name of the Span.
   */
  void setName(absl::string_view name) { name_ = std::string(name); }

  /**
   * Add a key-value pair to either the Span's annotations or metadata.
   * A whitelist of keys are added to the annotations, everything else is added to the metadata.
   */
  void setTag(absl::string_view name, absl::string_view value) override {
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(value);
  }

  /**
   * Set the ID of the parent segment. This is different from the Trace ID.
   * The parent ID is used if the request originated from an instrumented application.
   * For more information see:
   * https://docs.aws.amazon.com/xray/latest/devguide/xray-concepts.html#xray-concepts-tracingheader
   */
  void setParentId(absl::string_view parent_segment_id) {
    parent_segment_id_ = std::string(parent_segment_id);
  }

  /**
   * Set the recording start time of the traced operation/request.
   */
  void setStartTime(Envoy::SystemTime start_time) { start_time_ = start_time; }

  /**
   * Set the recording end time of the traced operation/request.
   */
  void setEndTime(Envoy::SystemTime end_time) { end_time_ = end_time; }

  /**
   * Set the Span ID.
   * This ID is used as the (sub)segment ID.
   * A single Trace can have Multiple segments and each segment can have multiple sub-segments.
   */
  void setId(uint64_t id) { id_ = id; }

  /**
   * Not used by X-Ray.
   */
  void setSampled(bool sampled) override { UNREFERENCED_PARAMETER(sampled); };

  /**
   * Not used by X-Ray.
   */
  void injectContext(Http::HeaderMap& request_headers) override;

  /**
   * Get the start time of this Span.
   */
  Envoy::SystemTime startTime() const { return start_time_; }

  /**
   * Get the end time of this Span.
   */
  Envoy::SystemTime endTime() const { return end_time_; }

  /**
   * Get this Span's ID.
   */
  uint64_t Id() const { return id_; }

  /**
   * Not used by X-Ray because the Spans are "logged" (serialized) to the X-Ray daemon.
   */
  void log(Envoy::SystemTime, const std::string&) override {}

  /**
   * Not supported by X-Ray.
   */
  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string&,
                              Envoy::SystemTime) override {
    return nullptr;
  }

private:
  void setParentSpan(Tracing::Span* parent) { parent_span_ = parent; }

  TimeSource& time_source_;
  Envoy::SystemTime start_time_;
  Envoy::SystemTime end_time_;
  std::string operation_name_;
  std::string trace_id_;
  std::string parent_segment_id_;
  std::string name_;
  using Annotation = std::pair<std::string, std::string>;
  absl::flat_hash_map<std::string, std::vector<Annotation>> annotations_;
  std::vector<std::pair<std::string, std::string>> metadata_;
  Tracing::Span* parent_span_;
  uint64_t id_;
};

class Tracer {
public:
  Tracer(absl::string_view segment_name, TimeSource& time_source)
      : segment_name_(segment_name), time_source_(time_source) {
    UNREFERENCED_PARAMETER(time_source_);
  }

  /**
   * Starts a tracing span for X-Ray
   */
  Tracing::SpanPtr startSpan(const std::string& span_name, const std::string& operation_name,
                             Envoy::SystemTime start_time,
                             const absl::optional<XRayHeader>& xray_header);

private:
  const std::string segment_name_;
  TimeSource& time_source_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
