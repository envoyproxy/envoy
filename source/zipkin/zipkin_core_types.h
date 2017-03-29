#pragma once

#include <string>
#include <vector>
#include <memory>

#include "util.h"
#include "tracer_interface.h"

namespace Zipkin {

class ZipkinBase {
public:
  virtual ~ZipkinBase() {}

  virtual const std::string& toJson() = 0;

protected:
  std::string json_string_;
};

class Endpoint : public ZipkinBase {
public:
  Endpoint(const Endpoint&);

  Endpoint& operator=(const Endpoint&);

  Endpoint() : ipv4_(), port_(0), service_name_(), isset_ipv6_(false) {}

  Endpoint(std::string& ipv4, uint16_t port, const std::string& service_name)
      : ipv4_(ipv4), port_(port), service_name_(service_name), isset_ipv6_(false) {}

  const std::string& ipv4() const { return ipv4_; }

  void setIpv4(std::string& ipv4) { ipv4_ = ipv4; }

  const std::string& ipv6() const { return ipv6_; }

  void setIpv6(const std::string& ipv6) {
    ipv6_ = ipv6;
    isset_ipv6_ = true;
  }

  bool isSetIpv6() const { return isset_ipv6_; }

  uint16_t port() const { return port_; }

  void setPort(uint16_t port) { port_ = port; }

  const std::string& serviceName() const { return service_name_; }

  void setServiceName(const std::string& service_name) { service_name_ = service_name; }

  const std::string& toJson() override;

private:
  std::string ipv4_;
  uint16_t port_;
  std::string service_name_;
  std::string ipv6_;

  bool isset_ipv6_;
};

class Annotation : public ZipkinBase {
public:
  Annotation(const Annotation&);

  Annotation& operator=(const Annotation&);

  Annotation() : timestamp_(0), value_(), isset_host_(false) {}

  Annotation(uint64_t timestamp, const std::string value, Endpoint& host)
      : timestamp_(timestamp), value_(value), host_(host), isset_host_(true) {}

  const Endpoint& host() const { return host_; }

  void setHost(const Endpoint& host) {
    host_ = host;
    isset_host_ = true;
  }

  uint64_t timestamp() const { return timestamp_; }

  void setTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }

  const std::string& value() const { return value_; }

  void setValue(const std::string& value) { value_ = value; }

  bool isSetHost() const { return isset_host_; }

  const std::string& toJson() override;

private:
  uint64_t timestamp_;
  std::string value_;
  Endpoint host_;

  bool isset_host_;
};

enum class AnnotationType {
  BOOL = 0,
  BYTES = 1,
  I16 = 2,
  I32 = 3,
  I64 = 4,
  DOUBLE = 5,
  STRING = 6
};

class BinaryAnnotation : public ZipkinBase {
public:
  BinaryAnnotation(const BinaryAnnotation&);

  BinaryAnnotation& operator=(const BinaryAnnotation&);

  BinaryAnnotation() : key_(), value_(), annotation_type_(), isset_host_(false) {}

  AnnotationType annotationType() const { return annotation_type_; }

  void setAnnotationType(AnnotationType annotationType) { annotation_type_ = annotationType; }

  const Endpoint& host() const { return host_; }

  void setHost(const Endpoint& host) {
    host_ = host;
    isset_host_ = true;
  }

  bool isSetHost() const { return isset_host_; }

  const std::string& key() const { return key_; }

  void setKey(const std::string& key) { key_ = key; }

  const std::string& value() const { return value_; }

  void setValue(const std::string& value) { value_ = value; }

  const std::string& toJson() override;

private:
  std::string key_;
  std::string value_;
  Endpoint host_;

  AnnotationType annotation_type_;

  bool isset_host_;
};

typedef struct _Span__isset {
  _Span__isset()
      : parent_id(false), debug(false), timestamp(false), duration(false), trace_id_high(false) {}
  bool parent_id : 1;
  bool debug : 1;
  bool timestamp : 1;
  bool duration : 1;
  bool trace_id_high : 1;
} _Span__isset;

class Span : public ZipkinBase {
public:
  Span(const Span&);

  Span& operator=(const Span&);

  Span()
      : trace_id_(0), name_(), id_(0), parent_id_(0), debug_(false), timestamp_(0), duration_(0),
        trace_id_high_(0), start_time_(0), tracer_(nullptr) {}

  void setTraceId(const uint64_t val) { trace_id_ = val; }

  void setName(const std::string& val) { name_ = val; }

  void setId(const uint64_t val) { id_ = val; }

  void setParentId(const uint64_t val) {
    parent_id_ = val;
    isset_.parent_id = true;
  }

  void setAannotations(const std::vector<Annotation>& val) { annotations_ = val; }

  void addAnnotation(const Annotation& ann) { annotations_.push_back(ann); }

  void setBinaryAnnotations(const std::vector<BinaryAnnotation>& val) { binary_annotations_ = val; }

  void addBinaryAnnotation(const BinaryAnnotation& bann) { binary_annotations_.push_back(bann); }

  void setDebug(const bool val) {
    debug_ = val;
    isset_.debug = true;
  }

  void setTimestamp(const int64_t val) {
    timestamp_ = val;
    isset_.timestamp = true;
  }

  void setDuration(const int64_t val) {
    duration_ = val;
    isset_.duration = true;
  }

  void setTraceIdHigh(const int64_t val) {
    trace_id_high_ = val;
    isset_.trace_id_high = true;
  }

  void setStartTime(const int64_t time) { start_time_ = time; }

  const std::vector<Annotation>& annotations() const { return annotations_; }

  const std::vector<BinaryAnnotation>& binaryAnnotations() const { return binary_annotations_; }

  bool isDebug() const { return debug_; }

  int64_t duration() const { return duration_; }

  uint64_t id() const { return id_; }

  std::string idAsHexString() const { return Util::uint64ToBase16(id_); }

  const _Span__isset& isSet() const { return isset_; }

  const std::string& name() const { return name_; }

  uint64_t parentId() const { return parent_id_; }

  std::string parentIdAsHexString() const { return Util::uint64ToBase16(parent_id_); }

  int64_t timestamp() const { return timestamp_; }

  uint64_t traceId() const { return trace_id_; }

  std::string traceIdAsHexString() const { return Util::uint64ToBase16(trace_id_); }

  uint64_t traceIdHigh() const { return trace_id_high_; }

  int64_t startTime() const { return start_time_; }

  const std::string& toJson() override;

  void setTracer(TracerPtr tracer) { tracer_ = tracer; }

  TracerPtr tracer() const { return tracer_; }

  void finish();

  void setTag(const std::string& name, const std::string& value);

private:
  uint64_t trace_id_;
  std::string name_;
  uint64_t id_;
  uint64_t parent_id_;
  std::vector<Annotation> annotations_;
  std::vector<BinaryAnnotation> binary_annotations_;
  bool debug_;
  int64_t timestamp_;
  int64_t duration_;
  uint64_t trace_id_high_;

  int64_t start_time_;

  TracerPtr tracer_;

  _Span__isset isset_;
};
} // Zipkin
