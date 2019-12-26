#pragma once

#include <list>
#include <string>

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Endpoint is an endpoint attribution on an Annotation or BinaryAnnotation.
 */
class Endpoint {
public:
  Endpoint(int32_t ipv4, int16_t port, const std::string& service_name)
      : ipv4_(ipv4), port_(port), service_name_(service_name) {}
  Endpoint() = default;

  int32_t ipv4_{0};
  int16_t port_{0};
  std::string service_name_;
};

/**
 * Annotation is a span annotation.
 */
class Annotation {
public:
  Annotation(int64_t timestamp, const std::string& value, absl::optional<Endpoint> host)
      : timestamp_(timestamp), value_(value), host_(host) {}
  Annotation() = default;

  int64_t timestamp_{0};
  std::string value_;
  absl::optional<Endpoint> host_;
};
using AnnotationList = std::list<Annotation>;

/**
 * AnnotationType represents a BinaryAnnotation's type.
 */
enum class AnnotationType {
  Bool = 0,
  Bytes = 1,
  I16 = 2,
  I32 = 3,
  I64 = 4,
  Double = 5,
  String = 6,
};

/**
 * BinaryAnnotation is a binary span annotation.
 */
class BinaryAnnotation {
public:
  BinaryAnnotation(const std::string& key, const std::string& value, AnnotationType annotation_type,
                   absl::optional<Endpoint> host)
      : key_(key), value_(value), annotation_type_(annotation_type), host_(host) {}
  BinaryAnnotation() = default;

  std::string key_;
  std::string value_;
  AnnotationType annotation_type_{AnnotationType::Bool};
  absl::optional<Endpoint> host_;
};
using BinaryAnnotationList = std::list<BinaryAnnotation>;

/**
 * Span is a single, annotated span in a trace.
 */
class Span {
public:
  Span(int64_t trace_id, const std::string& name, int64_t span_id,
       absl::optional<int64_t> parent_span_id, AnnotationList&& annotations,
       BinaryAnnotationList&& binary_annotations, bool debug)
      : trace_id_(trace_id), name_(name), span_id_(span_id), parent_span_id_(parent_span_id),
        annotations_(std::move(annotations)), binary_annotations_(std::move(binary_annotations)),
        debug_(debug) {}
  Span() = default;

  int64_t trace_id_{0};
  std::string name_;
  int64_t span_id_{0};
  absl::optional<int64_t> parent_span_id_;
  AnnotationList annotations_;
  BinaryAnnotationList binary_annotations_;
  bool debug_{false};
};
using SpanList = std::list<Span>;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
