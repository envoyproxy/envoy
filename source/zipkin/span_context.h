#pragma once

#include <vector>
#include <string>

#include "zipkin_core_types.h"
#include "util.h"

namespace Zipkin {

typedef struct Annotation_isset_ {
  Annotation_isset_() : cs(false), cr(false), ss(false), sr(false) {}
  bool cs : 1;
  bool cr : 1;
  bool ss : 1;
  bool sr : 1;
} Annotation_isset_;

class SpanContext {
public:
  SpanContext() : trace_id_(0), id_(0), parent_id_(0), is_populated_(false) {}

  SpanContext(const Span& span);

  virtual ~SpanContext() {}

  const std::string serializeToString();

  void populateFromString(std::string s);

  uint64_t id() const { return id_; }

  std::string idAsHexString() const { return Util::uint64ToBase16(id_); }

  uint64_t parent_id() const { return parent_id_; }

  std::string parentIdAsHexString() const { return Util::uint64ToBase16(parent_id_); }

  uint64_t trace_id() const { return trace_id_; }

  std::string traceIdAsHexString() const { return Util::uint64ToBase16(trace_id_); }

  Annotation_isset_ isSetAnnotation() const { return annotation_values_; }

private:
  const static std::string FIELD_SEPARATOR_;
  uint64_t trace_id_;
  uint64_t id_;
  uint64_t parent_id_;
  Annotation_isset_ annotation_values_;
  bool is_populated_;
};
} // Zipkin
