#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/hex.h"

#include "extensions/tracers/xray/daemon_broker.h"
#include "extensions/tracers/xray/sampling_strategy.h"
#include "extensions/tracers/xray/xray_configuration.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

constexpr auto XRayTraceHeader = "x-amzn-trace-id";

class Span : public Tracing::Span {
public:
  /**
   * Creates a new Span.
   *
   * @param time_source A time source to create Span IDs using the monotonic clock.
   * @param broker Facilitates communication with the X-Ray daemon.
   */
  Span(TimeSource& time_source, DaemonBroker& broker)
      : time_source_(time_source), broker_(broker), sampled_(true) {}

  /**
   * Sets the Span's trace ID.
   */
  void setTraceId(absl::string_view trace_id) { trace_id_ = std::string(trace_id); };

  /**
   * Gets the Span's trace ID.
   */
  const std::string& traceId() const { return trace_id_; }

  /**
   * Completes the current span, serialize it and send it to the X-Ray daemon.
   */
  void finishSpan() override;

  /**
   * Sets the current operation name on the Span.
   * This information will be included in the X-Ray span's metadata.
   */
  void setOperation(absl::string_view operation) override {
    operation_name_ = std::string(operation);
  }

  /**
   * Sets the name of the Span.
   */
  void setName(absl::string_view name) { name_ = std::string(name); }

  /**
   * Adds a key-value pair to either the Span's annotations or metadata.
   * A whitelist of keys are added to the annotations, everything else is added to the metadata.
   */
  void setTag(absl::string_view name, absl::string_view value) override;

  /**
   * Sets the ID of the parent segment. This is different from the Trace ID.
   * The parent ID is used if the request originated from an instrumented application.
   * For more information see:
   * https://docs.aws.amazon.com/xray/latest/devguide/xray-concepts.html#xray-concepts-tracingheader
   */
  void setParentId(absl::string_view parent_segment_id) {
    parent_segment_id_ = std::string(parent_segment_id);
  }

  /**
   * Sets the recording start time of the traced operation/request.
   */
  void setStartTime(Envoy::SystemTime start_time) { start_time_ = start_time; }

  /**
   * Sets the Span ID.
   * This ID is used as the (sub)segment ID.
   * A single Trace can have Multiple segments and each segment can have multiple sub-segments.
   * The id is converted to a hexadecimal string internally.
   */
  void setId(uint64_t id);

  /**
   * Marks the span as either "sampled" or "not-sampled".
   * By default, Spans are "sampled".
   * This is handy in cases where the sampling decision has already been determined either by Envoy
   * or by a downstream service.
   */
  void setSampled(bool sampled) override { sampled_ = sampled; };

  /**
   * Adds X-Ray trace header to the set of outgoing headers.
   */
  void injectContext(Http::RequestHeaderMap& request_headers) override;

  /**
   * Gets the start time of this Span.
   */
  Envoy::SystemTime startTime() const { return start_time_; }

  /**
   * Gets this Span's ID.
   */
  const std::string& Id() const { return id_; }

  const std::string& parentId() const { return parent_segment_id_; }

  /**
   * Gets this Span's name.
   */
  const std::string& name() const { return name_; }

  /**
   * Determines whether this span is sampled.
   */
  bool sampled() const { return sampled_; }

  /**
   * Not used by X-Ray because the Spans are "logged" (serialized) to the X-Ray daemon.
   */
  void log(Envoy::SystemTime, const std::string&) override {}

  /**
   * Creates a child span.
   * In X-Ray terms this creates a sub-segment and sets its parent ID to the current span's ID.
   * @param operation_name The span of the child span.
   * @param start_time The time at which this child span started.
   */
  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& operation_name,
                              Envoy::SystemTime start_time) override;

private:
  Envoy::SystemTime start_time_;
  std::string operation_name_;
  std::string id_;
  std::string trace_id_;
  std::string parent_segment_id_;
  std::string name_;
  absl::flat_hash_map<std::string, std::string> http_request_annotations_;
  absl::flat_hash_map<std::string, std::string> http_response_annotations_;
  absl::flat_hash_map<std::string, std::string> custom_annotations_;
  Envoy::TimeSource& time_source_;
  DaemonBroker& broker_;
  bool sampled_;
};

using SpanPtr = std::unique_ptr<Span>;

class Tracer {
public:
  Tracer(absl::string_view segment_name, DaemonBrokerPtr daemon_broker, TimeSource& time_source)
      : segment_name_(segment_name), daemon_broker_(std::move(daemon_broker)),
        time_source_(time_source) {}

  /**
   * Starts a tracing span for X-Ray
   */
  Tracing::SpanPtr startSpan(const std::string& operation_name, Envoy::SystemTime start_time,
                             const absl::optional<XRayHeader>& xray_header);
  /**
   * Creates a Span that is marked as not-sampled.
   * This is useful when the sampling decision is done in Envoy's X-Ray and we want to avoid
   * overruling that decision in the upstream service in case that service itself uses X-Ray for
   * tracing.
   */
  XRay::SpanPtr createNonSampledSpan() const;

private:
  const std::string segment_name_;
  const DaemonBrokerPtr daemon_broker_;
  Envoy::TimeSource& time_source_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
