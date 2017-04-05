#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "zipkin/zipkin_core_types.h"
#include "zipkin/zipkin_core_constants.h"
#include "zipkin/zipkin_json_field_names.h"
#include "zipkin/util.h"
#include "zipkin/span_context.h"

#include <iostream>

namespace Zipkin {

Endpoint::Endpoint(const Endpoint& ep) {
  ipv4_ = ep.ipv4();
  port_ = ep.port();
  service_name_ = ep.serviceName();
  ipv6_ = ep.ipv6();
  isset_ipv6_ = ep.isSetIpv6();
}

Endpoint& Endpoint::operator=(const Endpoint& ep) {
  ipv4_ = ep.ipv4();
  port_ = ep.port();
  service_name_ = ep.serviceName();
  ipv6_ = ep.ipv6();
  isset_ipv6_ = ep.isSetIpv6();

  return *this;
}

const std::string& Endpoint::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::ENDPOINT_IPV4.c_str());
  writer.String(ipv4_.c_str());
  writer.Key(ZipkinJsonFieldNames::ENDPOINT_PORT.c_str());
  writer.Uint(port_);
  writer.Key(ZipkinJsonFieldNames::ENDPOINT_SERVICE_NAME.c_str());
  writer.String(service_name_.c_str());
  if (isset_ipv6_) {
    writer.Key(ZipkinJsonFieldNames::ENDPOINT_IPV6.c_str());
    writer.String(ipv6_.c_str());
  }
  writer.EndObject();
  json_string_ = s.GetString();

  return json_string_;
}

Annotation::Annotation(const Annotation& ann) {
  timestamp_ = ann.timestamp();
  value_ = ann.value();
  host_ = ann.host();
  isset_host_ = ann.isSetHost();
}

Annotation& Annotation::operator=(const Annotation& ann) {
  timestamp_ = ann.timestamp();
  value_ = ann.value();
  host_ = ann.host();
  isset_host_ = ann.isSetHost();

  return *this;
}

const std::string& Annotation::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::ANNOTATION_TIMESTAMP.c_str());
  writer.Uint64(timestamp_);
  writer.Key(ZipkinJsonFieldNames::ANNOTATION_VALUE.c_str());
  writer.String(value_.c_str());
  writer.EndObject();

  json_string_ = s.GetString();

  if (isset_host_) {
    Util::mergeJsons(json_string_, host_.toJson(),
                     ZipkinJsonFieldNames::ANNOTATION_ENDPOINT.c_str());
  }

  return json_string_;
}

BinaryAnnotation::BinaryAnnotation(const BinaryAnnotation& ann) {
  key_ = ann.key();
  value_ = ann.value();
  annotation_type_ = ann.annotationType();
  host_ = ann.host();
  isset_host_ = ann.isSetHost();
}

BinaryAnnotation& BinaryAnnotation::operator=(const BinaryAnnotation& ann) {
  key_ = ann.key();
  value_ = ann.value();
  annotation_type_ = ann.annotationType();
  host_ = ann.host();
  isset_host_ = ann.isSetHost();

  return *this;
}

const std::string& BinaryAnnotation::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::BINARY_ANNOTATION_KEY.c_str());
  writer.String(key_.c_str());
  writer.Key(ZipkinJsonFieldNames::BINARY_ANNOTATION_VALUE.c_str());
  writer.String(value_.c_str());
  writer.EndObject();

  json_string_ = s.GetString();

  if (isset_host_) {
    Util::mergeJsons(json_string_, host_.toJson(),
                     ZipkinJsonFieldNames::BINARY_ANNOTATION_ENDPOINT.c_str());
  }

  return json_string_;
}

Span::Span(const Span& span) {
  trace_id_ = span.traceId();
  name_ = span.name();
  id_ = span.id();
  parent_id_ = span.parentId();
  annotations_ = span.annotations();
  binary_annotations_ = span.binaryAnnotations();
  debug_ = span.isDebug();
  timestamp_ = span.timestamp();
  duration_ = span.duration();
  trace_id_high_ = span.traceIdHigh();
  isset_ = span.isSet();
  start_time_ = span.startTime();
  tracer_ = span.tracer();
}

Span& Span::operator=(const Span& span) {
  trace_id_ = span.traceId();
  name_ = span.name();
  id_ = span.id();
  parent_id_ = span.parentId();
  annotations_ = span.annotations();
  binary_annotations_ = span.binaryAnnotations();
  debug_ = span.isDebug();
  timestamp_ = span.timestamp();
  duration_ = span.duration();
  trace_id_high_ = span.traceIdHigh();
  isset_ = span.isSet();
  start_time_ = span.startTime();
  tracer_ = span.tracer();

  return *this;
}

const std::string& Span::toJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key(ZipkinJsonFieldNames::SPAN_TRACE_ID.c_str());
  writer.String(Util::uint64ToBase16(trace_id_).c_str());
  writer.Key(ZipkinJsonFieldNames::SPAN_NAME.c_str());
  writer.String(name_.c_str());
  writer.Key(ZipkinJsonFieldNames::SPAN_ID.c_str());
  writer.String(Util::uint64ToBase16(id_).c_str());

  if (isset_.parent_id && parent_id_) {
    writer.Key(ZipkinJsonFieldNames::SPAN_PARENT_ID.c_str());
    writer.String(Util::uint64ToBase16(parent_id_).c_str());
  }

  if (isset_.timestamp) {
    writer.Key(ZipkinJsonFieldNames::SPAN_TIMESTAMP.c_str());
    writer.Int64(timestamp_);
  }

  if (isset_.duration) {
    writer.Key(ZipkinJsonFieldNames::SPAN_DURATION.c_str());
    writer.Int64(duration_);
  }

  if (isset_.debug) {
    writer.Key(ZipkinJsonFieldNames::SPAN_DEBUG.c_str());
    writer.Bool(debug_);
  }

  if (isset_.trace_id_high) {
    writer.Key(ZipkinJsonFieldNames::SPAN_TRACE_ID_HIGH.c_str());
    writer.Int64(trace_id_high_);
  }

  writer.EndObject();

  json_string_ = s.GetString();

  std::cerr << "Will convert annotations to JSON" << std::endl;

  std::vector<const std::string*> annotation_json_vector;

  for (auto it = annotations_.begin(); it != annotations_.end(); it++) {
    annotation_json_vector.push_back(&(it->toJson()));
  }
  Util::addArrayToJson(json_string_, annotation_json_vector,
                       ZipkinJsonFieldNames::SPAN_ANNOTATIONS.c_str());

  std::vector<const std::string*> binary_annotation_json_vector;
  for (auto it = binary_annotations_.begin(); it != binary_annotations_.end(); it++) {
    binary_annotation_json_vector.push_back(&(it->toJson()));
  }
  Util::addArrayToJson(json_string_, binary_annotation_json_vector,
                       ZipkinJsonFieldNames::SPAN_BINARY_ANNOTATIONS.c_str());

  return json_string_;
}

void Span::finish() {
  // Assumption: Span will have only one annotation when this method is called
  SpanContext context(*this);
  if ((context.isSetAnnotation().sr) && (!context.isSetAnnotation().ss)) {
    // Need to set the SS annotation
    Annotation ss;
    ss.setHost(annotations_[0].host());
    ss.setTimestamp(Util::timeSinceEpochMicro());
    ss.setValue(ZipkinCoreConstants::SERVER_SEND);
    annotations_.push_back(std::move(ss));
  } else if ((context.isSetAnnotation().cs) && (!context.isSetAnnotation().cr)) {
    // Need to set the CR annotation
    Annotation cr;
    uint64_t stop_time = Util::timeSinceEpochMicro();
    cr.setHost(annotations_[0].host());
    cr.setTimestamp(stop_time);
    cr.setValue(ZipkinCoreConstants::CLIENT_RECV);
    annotations_.push_back(std::move(cr));
    setDuration(stop_time - timestamp_);
  }

  std::cerr << "Span: " << toJson() << std::endl;
  auto t = tracer();
  if (t) {
    std::cerr << "Will call Tracer::reportSpan" << std::endl;
    t->reportSpan(std::move(*this));
  }
}

void Span::setTag(const std::string& name, const std::string& value) {
  std::cerr << "setTag called --> Name: " << name << "; Value: " << value << std::endl;
  if ((name.size() > 0) && (value.size() > 0)) {
    addBinaryAnnotation(BinaryAnnotation(name, value));
  }
}
} // Zipkin
