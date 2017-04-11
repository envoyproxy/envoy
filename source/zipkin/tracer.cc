#include <regex>

#include "zipkin/util.h"
#include "zipkin/tracer.h"
#include "zipkin/zipkin_core_constants.h"

namespace Zipkin {

Span Tracer::startSpan(const std::string& span_name, uint64_t start_time) {
  Span span;
  Annotation cs;
  std::string ip;
  uint16_t port;
  Endpoint ep;
  uint64_t timestampMicro;

  // Build the endpoint
  getIPAndPort(address_, ip, port);
  ep.setIpv4(ip);
  ep.setPort(port);
  ep.setServiceName(service_name_);

  // Build the CS annotation
  cs.setEndpoint(std::move(ep));
  cs.setValue(ZipkinCoreConstants::CLIENT_SEND);

  // Create an all-new span, with no parent id
  span.setName(span_name);
  uint64_t randonNumber = Util::generateRandom64();
  span.setId(randonNumber);
  span.setTraceId(randonNumber);
  span.setStartTime(start_time);

  // Set the timestamp globally for the span and also for the CS annotation
  timestampMicro = Util::timeSinceEpochMicro();
  cs.setTimestamp(timestampMicro);
  span.setTimestamp(timestampMicro);

  // Add CS annotation to the span
  span.addAnnotation(std::move(cs));

  span.setTracer(this);

  return span;
}

Span Tracer::startSpan(const std::string& span_name, uint64_t start_time,
                       SpanContext& previous_context) {
  Span span;
  Annotation annotation;
  std::string ip;
  uint16_t port;
  Endpoint ep;
  uint64_t timestampMicro;

  // TODO We currently ignore the start_time to set the span/annotation timestamps
  // Is start_time really needed?
  timestampMicro = Util::timeSinceEpochMicro();

  if ((previous_context.isSetAnnotation().sr) && (!previous_context.isSetAnnotation().cs)) {
    // We need to create a new span that is a child of the previous span; no shared context

    // Create a new span id
    uint64_t randonNumber = Util::generateRandom64();
    span.setId(randonNumber);

    span.setName(span_name);

    // Set the parent id to the id of the previous span
    span.setParentId(previous_context.id());

    // Set the CS annotation value
    annotation.setValue(ZipkinCoreConstants::CLIENT_SEND);

    // Set the timestamp globally for the span
    span.setTimestamp(timestampMicro);
  } else if ((previous_context.isSetAnnotation().cs) && (!previous_context.isSetAnnotation().sr)) {
    // We need to create a new span that will share context with the previous span

    // Initialize the shared context for the new span
    span.setId(previous_context.id());
    if (previous_context.parent_id()) {
      span.setParentId(previous_context.parent_id());
    }

    // Set the SR annotation value
    annotation.setValue(ZipkinCoreConstants::SERVER_RECV);
  } else {
    // Unexpected condition

    // TODO Log an error
    return span; // return an empty span
  }

  // Build the endpoint
  getIPAndPort(address_, ip, port);
  ep.setIpv4(ip);
  ep.setPort(port);
  ep.setServiceName(service_name_);

  // Add the newly-created annotation to the span
  annotation.setEndpoint(std::move(ep));
  annotation.setTimestamp(timestampMicro);
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

void Tracer::setReporter(std::unique_ptr<Reporter> reporter) {
  reporter_ = std::shared_ptr<Reporter>(std::move(reporter));
}

void Tracer::getIPAndPort(const std::string& address, std::string& ip, uint16_t& port) {
  std::regex re("^(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})(:(\\d+))?$");
  std::smatch match;
  if (std::regex_search(address, match, re)) {
    ip = match.str(1);
    if (match.str(3).size() > 0) {
      port = std::stoi(match.str(3));
    }
  }
}
} // Zipkin
