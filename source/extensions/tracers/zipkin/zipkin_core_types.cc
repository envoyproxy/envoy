#include "extensions/tracers/zipkin/zipkin_core_types.h"

#include <vector>

#include "common/common/utility.h"

#include "extensions/tracers/zipkin/span_context.h"
#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_json_field_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

Endpoint::Endpoint(const Endpoint& ep) {
  service_name_ = ep.serviceName();
  address_ = ep.address();
}

Endpoint& Endpoint::operator=(const Endpoint& ep) {
  service_name_ = ep.serviceName();
  address_ = ep.address();
  return *this;
}

const ProtobufWkt::Struct Endpoint::toStruct() const {
  ProtobufWkt::Struct endpoint;
  auto* fields = endpoint.mutable_fields();
  if (!address_) {
    (*fields)[ENDPOINT_IPV4] = ValueUtil::stringValue("");
    (*fields)[ENDPOINT_PORT] = ValueUtil::numberValue(0);
  } else {
    (*fields)[address_->ip()->version() == Network::Address::IpVersion::v4 ? ENDPOINT_IPV4
                                                                           : ENDPOINT_IPV6] =
        ValueUtil::stringValue(address_->ip()->addressAsString());
    (*fields)[ENDPOINT_PORT] = ValueUtil::numberValue(address_->ip()->port());
  }
  (*fields)[ENDPOINT_SERVICE_NAME] = ValueUtil::stringValue(service_name_);

  return endpoint;
}

Annotation::Annotation(const Annotation& ann) {
  timestamp_ = ann.timestamp();
  value_ = ann.value();
  if (ann.isSetEndpoint()) {
    endpoint_ = ann.endpoint();
  }
}

Annotation& Annotation::operator=(const Annotation& ann) {
  timestamp_ = ann.timestamp();
  value_ = ann.value();
  if (ann.isSetEndpoint()) {
    endpoint_ = ann.endpoint();
  }

  return *this;
}

void Annotation::changeEndpointServiceName(const std::string& service_name) {
  if (endpoint_.has_value()) {
    endpoint_.value().setServiceName(service_name);
  }
}

const ProtobufWkt::Struct Annotation::toStruct() const {
  ProtobufWkt::Struct annotation;
  auto* fields = annotation.mutable_fields();
  (*fields)[ANNOTATION_TIMESTAMP] = ValueUtil::numberValue(timestamp_);
  (*fields)[ANNOTATION_VALUE] = ValueUtil::stringValue(value_);
  if (endpoint_.has_value()) {
    (*fields)[ANNOTATION_ENDPOINT] =
        ValueUtil::structValue(static_cast<Endpoint>(endpoint_.value()).toStruct());
  }
  return annotation;
}

BinaryAnnotation::BinaryAnnotation(const BinaryAnnotation& ann) {
  key_ = ann.key();
  value_ = ann.value();
  annotation_type_ = ann.annotationType();
  if (ann.isSetEndpoint()) {
    endpoint_ = ann.endpoint();
  }
}

BinaryAnnotation& BinaryAnnotation::operator=(const BinaryAnnotation& ann) {
  key_ = ann.key();
  value_ = ann.value();
  annotation_type_ = ann.annotationType();
  if (ann.isSetEndpoint()) {
    endpoint_ = ann.endpoint();
  }

  return *this;
}

const ProtobufWkt::Struct BinaryAnnotation::toStruct() const {
  ProtobufWkt::Struct binary_annotation;
  auto* fields = binary_annotation.mutable_fields();
  (*fields)[BINARY_ANNOTATION_KEY] = ValueUtil::stringValue(key_);
  (*fields)[BINARY_ANNOTATION_VALUE] = ValueUtil::stringValue(value_);

  if (endpoint_) {
    (*fields)[BINARY_ANNOTATION_ENDPOINT] =
        ValueUtil::structValue(static_cast<Endpoint>(endpoint_.value()).toStruct());
  }

  return binary_annotation;
}

const std::string Span::EMPTY_HEX_STRING_ = "0000000000000000";

Span::Span(const Span& span) : time_source_(span.time_source_) {
  trace_id_ = span.traceId();
  if (span.isSetTraceIdHigh()) {
    trace_id_high_ = span.traceIdHigh();
  }
  name_ = span.name();
  id_ = span.id();
  if (span.isSetParentId()) {
    parent_id_ = span.parentId();
  }
  debug_ = span.debug();
  sampled_ = span.sampled();
  annotations_ = span.annotations();
  binary_annotations_ = span.binaryAnnotations();
  if (span.isSetTimestamp()) {
    timestamp_ = span.timestamp();
  }
  if (span.isSetDuration()) {
    duration_ = span.duration();
  }
  monotonic_start_time_ = span.startTime();
  tracer_ = span.tracer();
}

void Span::setServiceName(const std::string& service_name) {
  for (auto& annotation : annotations_) {
    annotation.changeEndpointServiceName(service_name);
  }
}

const ProtobufWkt::Struct Span::toStruct() const {
  ProtobufWkt::Struct span;
  auto* fields = span.mutable_fields();
  (*fields)[SPAN_TRACE_ID] = ValueUtil::stringValue(traceIdAsHexString());
  (*fields)[SPAN_NAME] = ValueUtil::stringValue(name_);
  (*fields)[SPAN_ID] = ValueUtil::stringValue(Hex::uint64ToHex(id_));

  if (parent_id_.has_value()) {
    (*fields)[SPAN_PARENT_ID] = ValueUtil::stringValue(Hex::uint64ToHex(parent_id_.value()));
  }

  if (timestamp_.has_value()) {
    (*fields)[SPAN_TIMESTAMP] = ValueUtil::numberValue(timestamp_.value());
  }

  if (duration_.has_value()) {
    (*fields)[SPAN_DURATION] = ValueUtil::numberValue(duration_.value());
  }

  if (!annotations_.empty()) {
    std::vector<ProtobufWkt::Value> annotation_list;
    for (auto& annotation : annotations_) {
      annotation_list.push_back(ValueUtil::structValue(annotation.toStruct()));
    }
    (*fields)[SPAN_ANNOTATIONS] = ValueUtil::listValue(annotation_list);
  }

  if (!binary_annotations_.empty()) {
    std::vector<ProtobufWkt::Value> binary_annotation_list;
    for (auto& binary_annotation : binary_annotations_) {
      binary_annotation_list.push_back(ValueUtil::structValue(binary_annotation.toStruct()));
    }
    (*fields)[SPAN_BINARY_ANNOTATIONS] = ValueUtil::listValue(binary_annotation_list);
  }

  return span;
}

void Span::finish() {
  // Assumption: Span will have only one annotation when this method is called.
  SpanContext context(*this);
  if (annotations_[0].value() == SERVER_RECV) {
    // Need to set the SS annotation
    Annotation ss;
    ss.setEndpoint(annotations_[0].endpoint());
    ss.setTimestamp(std::chrono::duration_cast<std::chrono::microseconds>(
                        time_source_.systemTime().time_since_epoch())
                        .count());
    ss.setValue(SERVER_SEND);
    annotations_.push_back(std::move(ss));
  } else if (annotations_[0].value() == CLIENT_SEND) {
    // Need to set the CR annotation.
    Annotation cr;
    const uint64_t stop_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                        time_source_.systemTime().time_since_epoch())
                                        .count();
    cr.setEndpoint(annotations_[0].endpoint());
    cr.setTimestamp(stop_timestamp);
    cr.setValue(CLIENT_RECV);
    annotations_.push_back(std::move(cr));
  }

  if (monotonic_start_time_) {
    const int64_t monotonic_stop_time = std::chrono::duration_cast<std::chrono::microseconds>(
                                            time_source_.monotonicTime().time_since_epoch())
                                            .count();
    setDuration(monotonic_stop_time - monotonic_start_time_);
  }

  if (auto t = tracer()) {
    t->reportSpan(std::move(*this));
  }
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (!name.empty() && !value.empty()) {
    addBinaryAnnotation(BinaryAnnotation(name, value));
  }
}

void Span::log(SystemTime timestamp, const std::string& event) {
  Annotation annotation;
  annotation.setTimestamp(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count());
  annotation.setValue(event);
  addAnnotation(std::move(annotation));
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
