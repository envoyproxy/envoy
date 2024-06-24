#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/tracing/tracer.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/codes.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/common_values.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/xray/daemon_broker.h"
#include "source/extensions/tracers/xray/sampling_strategy.h"
#include "source/extensions/tracers/xray/xray_configuration.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

constexpr absl::string_view SpanClientIp = "client_ip";
constexpr absl::string_view SpanXForwardedFor = "x_forwarded_for";
constexpr absl::string_view Subsegment = "subsegment";

const Tracing::TraceContextHandler& xRayTraceHeader();
const Tracing::TraceContextHandler& xForwardedForHeader();

class Span : public Tracing::Span, Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Creates a new Span.
   *
   * @param time_source A time source to get the span end time
   * @param random random generator for generating unique child span ids
   * @param broker Facilitates communication with the X-Ray daemon.
   */
  Span(TimeSource& time_source, Random::RandomGenerator& random, DaemonBroker& broker)
      : time_source_(time_source), random_(random), broker_(broker),
        id_(Hex::uint64ToHex(random_.random())) {}

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
   * Sets the current direction on the Span.
   * This information will be included in the X-Ray span's annotation.
   */
  void setDirection(absl::string_view direction) { direction_ = std::string(direction); }

  /**
   * Sets the name of the Span.
   */
  void setName(absl::string_view name) { name_ = std::string(name); }

  /**
   * Sets the origin of the Span.
   */
  void setOrigin(absl::string_view origin) { origin_ = std::string(origin); }

  /**
   * Gets the origin of the Span.
   */
  const std::string& origin() { return origin_; }

  /**
   * Adds a key-value pair to either the Span's annotations or metadata.
   * An allowlist of keys are added to the annotations, everything else is added to the metadata.
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
   * Sets the type of the Span. In X-Ray, an independent subsegment has a type of "subsegment".
   * https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html#api-segmentdocuments-subsegments
   */
  void setType(absl::string_view type) { type_ = std::string(type); }

  /**
   * Sets the aws metadata field of the Span.
   */
  void setAwsMetadata(const absl::flat_hash_map<std::string, ProtobufWkt::Value>& aws_metadata) {
    aws_metadata_ = aws_metadata;
  }

  /*
   * Adds to the http request annotation field of the Span.
   */
  void addToHttpRequestAnnotations(absl::string_view key, const ProtobufWkt::Value& value) {
    http_request_annotations_.emplace(std::string(key), value);
  }

  /*
   * Check if key is set in http request annotation field of a Span.
   */
  bool hasKeyInHttpRequestAnnotations(absl::string_view key) {
    return http_request_annotations_.find(key) != http_request_annotations_.end();
  }

  /*
   * Adds to the http response annotation field of the Span.
   */
  void addToHttpResponseAnnotations(absl::string_view key, const ProtobufWkt::Value& value) {
    http_response_annotations_.emplace(std::string(key), value);
  }

  /**
   * Sets the recording start time of the traced operation/request.
   */
  void setStartTime(Envoy::SystemTime start_time) { start_time_ = start_time; }

  /**
   * Marks the span as either "sampled" or "not-sampled".
   * By default, Spans are "sampled".
   * This is handy in cases where the sampling decision has already been determined either by Envoy
   * or by a downstream service.
   */
  void setSampled(bool sampled) override { sampled_ = sampled; };

  /**
   * Sets the server error as true for the traced operation/request.
   */
  void setServerError() { server_error_ = true; };

  /**
   * Sets the http response status code for the traced operation/request.
   */
  void setResponseStatusCode(uint64_t status_code) { response_status_code_ = status_code; };

  /**
   * Adds X-Ray trace header to the set of outgoing headers.
   */
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext&) override;

  /**
   * Gets the start time of this Span.
   */
  Envoy::SystemTime startTime() const { return start_time_; }

  /**
   * Gets this Span's ID.
   */
  const std::string& id() const { return id_; }

  /**
   * Gets this Span's parent ID.
   */
  const std::string& parentId() const { return parent_segment_id_; }

  /**
   * Gets this Span's direction.
   */
  const std::string& direction() const { return direction_; }

  /**
   * Gets this Span's type.
   */
  const std::string& type() const { return type_; }

  /**
   * Gets this Span's name.
   */
  const std::string& name() const { return name_; }

  /**
   * Determines whether this span is sampled.
   */
  bool sampled() const { return sampled_; }

  /**
   * Determines if a server error occurred (response status code was 5XX Server Error).
   */
  bool serverError() const { return server_error_; }

  /**
   * Determines if a client error occurred (response status code was 4XX Client Error).
   */
  bool clientError() const { return Http::CodeUtility::is4xx(response_status_code_); }

  /**
   * Determines if a request was throttled (response status code was 429 Too Many Requests).
   */
  bool isThrottled() const {
    return Http::Code::TooManyRequests == static_cast<Http::Code>(response_status_code_);
  }

  /**
   * Not used by X-Ray because the Spans are "logged" (serialized) to the X-Ray daemon.
   */
  void log(Envoy::SystemTime, const std::string&) override {}

  // X-Ray doesn't support baggage, so noop these OpenTracing functions.
  void setBaggage(absl::string_view, absl::string_view) override {}
  std::string getBaggage(absl::string_view) override { return EMPTY_STRING; }

  std::string getTraceId() const override { return trace_id_; };

  // TODO(#34412): This method is unimplemented for X-Ray.
  std::string getSpanId() const override { return EMPTY_STRING; };

  /**
   * Creates a child span.
   * In X-Ray terms this creates a sub-segment and sets its parent ID to the current span's ID.
   * @param operation_name The span of the child span.
   * @param start_time The time at which this child span started.
   */
  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& operation_name,
                              Envoy::SystemTime start_time) override;

private:
  Envoy::TimeSource& time_source_;
  Random::RandomGenerator& random_;
  DaemonBroker& broker_;
  Envoy::SystemTime start_time_;
  std::string operation_name_;
  std::string direction_;
  std::string id_;
  std::string trace_id_;
  std::string parent_segment_id_;
  std::string name_;
  std::string origin_;
  std::string type_;
  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
  absl::flat_hash_map<std::string, ProtobufWkt::Value> http_request_annotations_;
  absl::flat_hash_map<std::string, ProtobufWkt::Value> http_response_annotations_;
  absl::flat_hash_map<std::string, std::string> custom_annotations_;
  bool server_error_{false};
  uint64_t response_status_code_{0};
  bool sampled_{true};
};

using SpanPtr = std::unique_ptr<Span>;

class Tracer {
public:
  Tracer(absl::string_view segment_name, absl::string_view origin,
         const absl::flat_hash_map<std::string, ProtobufWkt::Value>& aws_metadata,
         DaemonBrokerPtr daemon_broker, TimeSource& time_source, Random::RandomGenerator& random)
      : segment_name_(segment_name), origin_(origin), aws_metadata_(aws_metadata),
        daemon_broker_(std::move(daemon_broker)), time_source_(time_source), random_(random) {}
  /**
   * Starts a tracing span for X-Ray
   */
  Tracing::SpanPtr startSpan(const Tracing::Config&, const std::string& operation_name,
                             Envoy::SystemTime start_time,
                             const absl::optional<XRayHeader>& xray_header,
                             const absl::optional<absl::string_view> client_ip);
  /**
   * Creates a Span that is marked as not-sampled.
   * This is useful when the sampling decision is done in Envoy's X-Ray and we want to avoid
   * overruling that decision in the upstream service in case that service itself uses X-Ray for
   * tracing. Also at the same time if X-Ray header is set then preserve its value.
   */
  XRay::SpanPtr createNonSampledSpan(const absl::optional<XRayHeader>& xray_header) const;

private:
  const std::string segment_name_;
  const std::string origin_;
  const absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
  const DaemonBrokerPtr daemon_broker_;
  Envoy::TimeSource& time_source_;
  Random::RandomGenerator& random_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
