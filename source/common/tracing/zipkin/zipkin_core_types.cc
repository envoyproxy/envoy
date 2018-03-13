#include "common/tracing/zipkin/zipkin_core_types.h"

#include "common/common/utility.h"
#include "common/tracing/zipkin/span_context.h"
#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"
#include "common/tracing/zipkin/zipkin_json_field_names.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Envoy {
// TODO(fabolive): Need to add interfaces to the JSON namespace

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

const std::string Endpoint::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  if (!address_) {
    writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_IPV4.c_str());
    writer.String("");
    writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_PORT.c_str());
    writer.Uint(0);
  } else {
    if (address_->ip()->version() == Network::Address::IpVersion::v4) {
      // IPv4
      writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_IPV4.c_str());
    } else {
      // IPv6
      writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_IPV6.c_str());
    }
    writer.String(address_->ip()->addressAsString().c_str());
    writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_PORT.c_str());
    writer.Uint(address_->ip()->port());
  }
  writer.Key(ZipkinJsonFieldNames::get().ENDPOINT_SERVICE_NAME.c_str());
  writer.String(service_name_.c_str());
  writer.EndObject();
  std::string json_string = s.GetString();

  return json_string;
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
  if (endpoint_) {
    endpoint_.value().setServiceName(service_name);
  }
}

const std::string Annotation::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::get().ANNOTATION_TIMESTAMP.c_str());
  writer.Uint64(timestamp_);
  writer.Key(ZipkinJsonFieldNames::get().ANNOTATION_VALUE.c_str());
  writer.String(value_.c_str());
  writer.EndObject();

  std::string json_string = s.GetString();

  if (endpoint_) {
    Util::mergeJsons(json_string, static_cast<Endpoint>(endpoint_.value()).toJson(),
                     ZipkinJsonFieldNames::get().ANNOTATION_ENDPOINT.c_str());
  }

  return json_string;
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

const std::string BinaryAnnotation::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::get().BINARY_ANNOTATION_KEY.c_str());
  writer.String(key_.c_str());
  writer.Key(ZipkinJsonFieldNames::get().BINARY_ANNOTATION_VALUE.c_str());
  writer.String(value_.c_str());
  writer.EndObject();

  std::string json_string = s.GetString();

  if (endpoint_) {
    Util::mergeJsons(json_string, static_cast<Endpoint>(endpoint_.value()).toJson(),
                     ZipkinJsonFieldNames::get().BINARY_ANNOTATION_ENDPOINT.c_str());
  }

  return json_string;
}

const std::string Span::EMPTY_HEX_STRING_ = "0000000000000000";

Span::Span(const Span& span) {
  trace_id_ = span.traceId();
  name_ = span.name();
  id_ = span.id();
  if (span.isSetParentId()) {
    parent_id_ = span.parentId();
  }
  debug_ = span.debug();
  annotations_ = span.annotations();
  binary_annotations_ = span.binaryAnnotations();
  if (span.isSetTimestamp()) {
    timestamp_ = span.timestamp();
  }
  if (span.isSetDuration()) {
    duration_ = span.duration();
  }
  if (span.isSetTraceIdHigh()) {
    trace_id_high_ = span.traceIdHigh();
  }
  monotonic_start_time_ = span.startTime();
  tracer_ = span.tracer();
}

void Span::setServiceName(const std::string& service_name) {
  for (auto it = annotations_.begin(); it != annotations_.end(); it++) {
    it->changeEndpointServiceName(service_name);
  }
}

const std::string Span::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::get().SPAN_TRACE_ID.c_str());
  writer.String(Hex::uint64ToHex(trace_id_).c_str());
  writer.Key(ZipkinJsonFieldNames::get().SPAN_NAME.c_str());
  writer.String(name_.c_str());
  writer.Key(ZipkinJsonFieldNames::get().SPAN_ID.c_str());
  writer.String(Hex::uint64ToHex(id_).c_str());

  if (parent_id_ && parent_id_.value()) {
    writer.Key(ZipkinJsonFieldNames::get().SPAN_PARENT_ID.c_str());
    writer.String(Hex::uint64ToHex(parent_id_.value()).c_str());
  }

  if (timestamp_) {
    writer.Key(ZipkinJsonFieldNames::get().SPAN_TIMESTAMP.c_str());
    writer.Int64(timestamp_.value());
  }

  if (duration_) {
    writer.Key(ZipkinJsonFieldNames::get().SPAN_DURATION.c_str());
    writer.Int64(duration_.value());
  }

  writer.EndObject();

  std::string json_string = s.GetString();

  std::vector<std::string> annotation_json_vector;

  for (auto it = annotations_.begin(); it != annotations_.end(); it++) {
    annotation_json_vector.push_back(it->toJson());
  }
  Util::addArrayToJson(json_string, annotation_json_vector,
                       ZipkinJsonFieldNames::get().SPAN_ANNOTATIONS.c_str());

  std::vector<std::string> binary_annotation_json_vector;
  for (auto it = binary_annotations_.begin(); it != binary_annotations_.end(); it++) {
    binary_annotation_json_vector.push_back(it->toJson());
  }
  Util::addArrayToJson(json_string, binary_annotation_json_vector,
                       ZipkinJsonFieldNames::get().SPAN_BINARY_ANNOTATIONS.c_str());

  return json_string;
}

void Span::finish() {
  // Assumption: Span will have only one annotation when this method is called
  SpanContext context(*this);
  if (annotations_[0].value() == ZipkinCoreConstants::get().SERVER_RECV) {
    // Need to set the SS annotation
    Annotation ss;
    ss.setEndpoint(annotations_[0].endpoint());
    ss.setTimestamp(std::chrono::duration_cast<std::chrono::microseconds>(
                        ProdSystemTimeSource::instance_.currentTime().time_since_epoch())
                        .count());
    ss.setValue(ZipkinCoreConstants::get().SERVER_SEND);
    annotations_.push_back(std::move(ss));
  } else if (annotations_[0].value() == ZipkinCoreConstants::get().CLIENT_SEND) {
    // Need to set the CR annotation
    Annotation cr;
    const uint64_t stop_timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(
            ProdSystemTimeSource::instance_.currentTime().time_since_epoch())
            .count();
    cr.setEndpoint(annotations_[0].endpoint());
    cr.setTimestamp(stop_timestamp);
    cr.setValue(ZipkinCoreConstants::get().CLIENT_RECV);
    annotations_.push_back(std::move(cr));
    const int64_t monotonic_stop_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            ProdMonotonicTimeSource::instance_.currentTime().time_since_epoch())
            .count();
    setDuration(monotonic_stop_time - monotonic_start_time_);
  }

  if (auto t = tracer()) {
    t->reportSpan(std::move(*this));
  }
}

void Span::setTag(const std::string& name, const std::string& value) {
  if (name.size() > 0 && value.size() > 0) {
    addBinaryAnnotation(BinaryAnnotation(name, value));
  }
}
} // namespace Zipkin
} // namespace Envoy
