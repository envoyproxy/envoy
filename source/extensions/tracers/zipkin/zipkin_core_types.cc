#include "source/extensions/tracers/zipkin/zipkin_core_types.h"

#include <vector>

#include "source/extensions/tracers/zipkin/span_context.h"
#include "source/extensions/tracers/zipkin/util.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"
#include "source/extensions/tracers/zipkin/zipkin_json_field_names.h"

#include "absl/strings/str_cat.h"
#include "fmt/format.h"

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

const Protobuf::Struct Endpoint::toStruct(Util::Replacements&) const {
  Protobuf::Struct endpoint;
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

const Protobuf::Struct Annotation::toStruct(Util::Replacements& replacements) const {
  Protobuf::Struct annotation;
  auto* fields = annotation.mutable_fields();
  (*fields)[ANNOTATION_TIMESTAMP] = Util::uint64Value(timestamp_, SPAN_TIMESTAMP, replacements);
  (*fields)[ANNOTATION_VALUE] = ValueUtil::stringValue(value_);
  if (endpoint_.has_value()) {
    (*fields)[ANNOTATION_ENDPOINT] =
        ValueUtil::structValue(static_cast<Endpoint>(endpoint_.value()).toStruct(replacements));
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

const Protobuf::Struct BinaryAnnotation::toStruct(Util::Replacements& replacements) const {
  Protobuf::Struct binary_annotation;
  auto* fields = binary_annotation.mutable_fields();
  (*fields)[BINARY_ANNOTATION_KEY] = ValueUtil::stringValue(key_);
  (*fields)[BINARY_ANNOTATION_VALUE] = ValueUtil::stringValue(value_);

  if (endpoint_) {
    (*fields)[BINARY_ANNOTATION_ENDPOINT] =
        ValueUtil::structValue(static_cast<Endpoint>(endpoint_.value()).toStruct(replacements));
  }

  return binary_annotation;
}

const std::string Span::EMPTY_HEX_STRING_ = "0000000000000000";

void Span::setServiceName(const std::string& service_name) {
  for (auto& annotation : annotations_) {
    annotation.changeEndpointServiceName(service_name);
  }
}

const Protobuf::Struct Span::toStruct(Util::Replacements& replacements) const {
  Protobuf::Struct span;
  auto* fields = span.mutable_fields();
  (*fields)[SPAN_TRACE_ID] = ValueUtil::stringValue(traceIdAsHexString());
  (*fields)[SPAN_NAME] = ValueUtil::stringValue(name_);
  (*fields)[SPAN_ID] = ValueUtil::stringValue(Hex::uint64ToHex(id_));

  if (parent_id_.has_value()) {
    (*fields)[SPAN_PARENT_ID] = ValueUtil::stringValue(Hex::uint64ToHex(parent_id_.value()));
  }

  if (timestamp_.has_value()) {
    // Usually we store number to a Protobuf::Struct object via ValueUtil::numberValue.
    // However, due to the possibility of rendering that to a number with scientific notation, we
    // chose to store it as a string and keeping track the corresponding replacement.
    (*fields)[SPAN_TIMESTAMP] = Util::uint64Value(timestamp_.value(), SPAN_TIMESTAMP, replacements);
  }

  if (duration_.has_value()) {
    // Since SPAN_DURATION has the same data type with SPAN_TIMESTAMP, we use Util::uint64Value to
    // store it.
    (*fields)[SPAN_DURATION] = Util::uint64Value(duration_.value(), SPAN_DURATION, replacements);
  }

  if (!annotations_.empty()) {
    std::vector<Protobuf::Value> annotation_list;
    annotation_list.reserve(annotations_.size());
    for (auto& annotation : annotations_) {
      annotation_list.push_back(ValueUtil::structValue(annotation.toStruct(replacements)));
    }
    (*fields)[SPAN_ANNOTATIONS] = ValueUtil::listValue(annotation_list);
  }

  if (!binary_annotations_.empty()) {
    std::vector<Protobuf::Value> binary_annotation_list;
    binary_annotation_list.reserve(binary_annotations_.size());
    for (auto& binary_annotation : binary_annotations_) {
      binary_annotation_list.push_back(
          ValueUtil::structValue(binary_annotation.toStruct(replacements)));
    }
    (*fields)[SPAN_BINARY_ANNOTATIONS] = ValueUtil::listValue(binary_annotation_list);
  }

  return span;
}

void Span::finishSpan() {
  // Assumption: Span will have only one annotation when this method is called.
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

  tracer_.reportSpan(std::move(*this));
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

void Span::injectContext(Tracing::TraceContext& trace_context, const Tracing::UpstreamContext&) {
  auto trace_context_option = tracer_.traceContextOption();

  // Always inject B3 headers
  ZipkinCoreConstants::get().X_B3_TRACE_ID.setRefKey(trace_context, traceIdAsHexString());
  ZipkinCoreConstants::get().X_B3_SPAN_ID.setRefKey(trace_context, idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure.
  if (isSetParentId()) {
    ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.setRefKey(trace_context, parentIdAsHexString());
  }

  // Set the sampled header.
  ZipkinCoreConstants::get().X_B3_SAMPLED.setRefKey(trace_context,
                                                    sampled() ? SAMPLED : NOT_SAMPLED);

  // Additionally inject W3C headers if dual propagation is enabled
  if (trace_context_option == envoy::config::trace::v3::ZipkinConfig::USE_B3_WITH_W3C_PROPAGATION) {
    injectW3CContext(trace_context);
  }
}
Tracing::SpanPtr Span::spawnChild(const Tracing::Config& config, const std::string& name,
                                  SystemTime start_time) {
  return tracer_.startSpan(config, name, start_time, spanContext());
}

SpanContext Span::spanContext() const {
  // The inner_context is set to true because this SpanContext is context of Envoy created span
  // rather than the one that extracted from the downstream request headers.
  return {trace_id_high_.value_or(0), trace_id_, id_, parent_id_.value_or(0), sampled_, true};
}

void Span::injectW3CContext(Tracing::TraceContext& trace_context) {
  // Convert Zipkin span context to W3C traceparent format
  // W3C traceparent format: 00-{trace-id}-{span-id}-{trace-flags}

  // Construct the 128-bit trace ID (32 hex chars)
  std::string trace_id_str;
  if (trace_id_high_.has_value() && trace_id_high_.value() != 0) {
    // We have a 128-bit trace ID, use both high and low parts
    trace_id_str = absl::StrCat(fmt::format("{:016x}", trace_id_high_.value()),
                                fmt::format("{:016x}", trace_id_));
  } else {
    // We have a 64-bit trace ID, pad with zeros for the high part
    trace_id_str = absl::StrCat("0000000000000000", fmt::format("{:016x}", trace_id_));
  }

  // Construct the traceparent header value in W3C format: version-traceid-spanid-flags
  std::string traceparent_value =
      fmt::format("00-{}-{:016x}-{}", trace_id_str, id_, sampled() ? "01" : "00");

  // Set the W3C traceparent header
  ZipkinCoreConstants::get().TRACE_PARENT.setRefKey(trace_context, traceparent_value);

  // For now, we don't set tracestate as it's optional and we don't have vendor-specific data
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
