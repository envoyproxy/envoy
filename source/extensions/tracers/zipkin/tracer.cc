#include "extensions/tracers/zipkin/tracer.h"

#include <chrono>

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& span_name,
                          SystemTime timestamp) {
  // Build the endpoint
  Endpoint ep(service_name_, address_);

  // Build the CS annotation
  Annotation cs;
  cs.setEndpoint(std::move(ep));
  if (config.operationName() == Tracing::OperationName::Egress) {
    cs.setValue(CLIENT_SEND);
  } else {
    cs.setValue(SERVER_RECV);
  }

  // Create an all-new span, with no parent id
  SpanPtr span_ptr = std::make_unique<Span>(time_source_);
  span_ptr->setName(span_name);
  uint64_t random_number = random_generator_.random();
  span_ptr->setId(random_number);
  span_ptr->setTraceId(random_number);
  if (trace_id_128bit_) {
    span_ptr->setTraceIdHigh(random_generator_.random());
  }
  int64_t start_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(
                                 time_source_.monotonicTime().time_since_epoch())
                                 .count();
  span_ptr->setStartTime(start_time_micro);

  // Set the timestamp globally for the span and also for the CS annotation
  uint64_t timestamp_micro =
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count();
  cs.setTimestamp(timestamp_micro);
  span_ptr->setTimestamp(timestamp_micro);

  // Add CS annotation to the span
  span_ptr->addAnnotation(std::move(cs));

  span_ptr->setTracer(this);

  return span_ptr;
}

SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& span_name,
                          SystemTime timestamp, const SpanContext& previous_context) {
  SpanPtr span_ptr = std::make_unique<Span>(time_source_);
  Annotation annotation;
  uint64_t timestamp_micro;

  timestamp_micro =
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count();

  span_ptr->setName(span_name);

  if (config.operationName() == Tracing::OperationName::Egress || !shared_span_context_) {
    // We need to create a new span that is a child of the previous span; no shared context

    // Create a new span id
    uint64_t random_number = random_generator_.random();
    span_ptr->setId(random_number);

    // Set the parent id to the id of the previous span
    span_ptr->setParentId(previous_context.id());

    // Set the CS annotation value
    annotation.setValue(CLIENT_SEND);

    // Set the timestamp globally for the span
    span_ptr->setTimestamp(timestamp_micro);
  } else if (config.operationName() == Tracing::OperationName::Ingress) {
    // We need to create a new span that will share context with the previous span

    // Initialize the shared context for the new span
    span_ptr->setId(previous_context.id());
    if (previous_context.parentId()) {
      span_ptr->setParentId(previous_context.parentId());
    }

    // Set the SR annotation value
    annotation.setValue(SERVER_RECV);
  } else {
    return span_ptr; // return an empty span
  }

  // Build the endpoint
  Endpoint ep(service_name_, address_);

  // Add the newly-created annotation to the span
  annotation.setEndpoint(std::move(ep));
  annotation.setTimestamp(timestamp_micro);
  span_ptr->addAnnotation(std::move(annotation));

  // Keep the same trace id
  span_ptr->setTraceId(previous_context.traceId());
  if (previous_context.is128BitTraceId()) {
    span_ptr->setTraceIdHigh(previous_context.traceIdHigh());
  }

  // Keep the same sampled flag
  span_ptr->setSampled(previous_context.sampled());

  int64_t start_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(
                                 time_source_.monotonicTime().time_since_epoch())
                                 .count();
  span_ptr->setStartTime(start_time_micro);

  span_ptr->setTracer(this);

  return span_ptr;
}

void Tracer::reportSpan(Span&& span) {
  if (reporter_ && span.sampled()) {
    reporter_->reportSpan(std::move(span));
  }
}

void Tracer::setReporter(ReporterPtr reporter) { reporter_ = std::move(reporter); }

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
