#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"
#include "common/tracing/zipkin/tracer.h"

namespace Zipkin {

Span Tracer::startSpan(const std::string& span_name, uint64_t start_time) {
  // Build the endpoint
  std::string ip;
  uint16_t port;
  Util::getIPAndPort(address_, ip, port);
  Endpoint ep;
  ep.setIpv4(ip);
  ep.setPort(port);
  ep.setServiceName(service_name_);

  // Build the CS annotation
  Annotation cs;
  cs.setEndpoint(std::move(ep));
  cs.setValue(ZipkinCoreConstants::CLIENT_SEND);

  // Create an all-new span, with no parent id
  Span span;
  span.setName(span_name);
  uint64_t randon_number = Util::generateRandom64();
  span.setId(randon_number);
  span.setTraceId(randon_number);
  span.setStartTime(start_time);

  // Set the timestamp globally for the span and also for the CS annotation
  uint64_t timestamp_micro;
  timestamp_micro = Util::timeSinceEpochMicro();
  cs.setTimestamp(timestamp_micro);
  span.setTimestamp(timestamp_micro);

  // Add CS annotation to the span
  span.addAnnotation(std::move(cs));

  span.setTracer(this);

  return span;
}

Span Tracer::startSpan(const std::string& span_name, uint64_t start_time,
                       SpanContext& previous_context) {
  Span span;
  Annotation annotation;
  uint64_t timestamp_micro;

  // TODO(fabolive) We currently ignore the start_time to set the span/annotation timestamps
  // Is start_time really needed?
  timestamp_micro = Util::timeSinceEpochMicro();

  if ((previous_context.isSetAnnotation().sr_) && (!previous_context.isSetAnnotation().cs_)) {
    // We need to create a new span that is a child of the previous span; no shared context

    // Create a new span id
    uint64_t randon_number = Util::generateRandom64();
    span.setId(randon_number);

    span.setName(span_name);

    // Set the parent id to the id of the previous span
    span.setParentId(previous_context.id());

    // Set the CS annotation value
    annotation.setValue(ZipkinCoreConstants::CLIENT_SEND);

    // Set the timestamp globally for the span
    span.setTimestamp(timestamp_micro);
  } else if ((previous_context.isSetAnnotation().cs_) &&
             (!previous_context.isSetAnnotation().sr_)) {
    // We need to create a new span that will share context with the previous span

    // Initialize the shared context for the new span
    span.setId(previous_context.id());
    if (previous_context.parent_id()) {
      span.setParentId(previous_context.parent_id());
    }

    // Set the SR annotation value
    annotation.setValue(ZipkinCoreConstants::SERVER_RECV);
  } else {
    return span; // return an empty span
  }

  // Build the endpoint
  std::string ip;
  uint16_t port;
  Util::getIPAndPort(address_, ip, port);
  Endpoint ep;
  ep.setIpv4(ip);
  ep.setPort(port);
  ep.setServiceName(service_name_);

  // Add the newly-created annotation to the span
  annotation.setEndpoint(std::move(ep));
  annotation.setTimestamp(timestamp_micro);
  span.addAnnotation(std::move(annotation));

  // Keep the same trace id
  span.setTraceId(previous_context.trace_id());

  span.setStartTime(start_time);

  span.setTracer(this);

  return span;
}

void Tracer::reportSpan(Span&& span) {
  auto r = reporter();
  if (r) {
    r->reportSpan(std::move(span));
  }
}

void Tracer::setReporter(ReporterUniquePtr reporter) {
  reporter_ = ReporterSharedPtr(std::move(reporter));
}
} // Zipkin
