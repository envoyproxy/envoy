#include <regex>
#include <string>
#include <cstring>

#include "zipkin/zipkin_core_constants.h"
#include "zipkin/span_context.h"

namespace Zipkin {

const std::string SpanContext::FIELD_SEPARATOR_ = ";";

SpanContext::SpanContext(const Span& span) {
  trace_id_ = span.traceId();
  id_ = span.id();
  parent_id_ = span.parentId();

  for (Annotation ann : span.annotations()) {
    if (ann.value() == ZipkinCoreConstants::CLIENT_RECV) {
      annotation_values_.cr = true;
    }
    if (ann.value() == ZipkinCoreConstants::CLIENT_SEND) {
      annotation_values_.cs = true;
    }
    if (ann.value() == ZipkinCoreConstants::SERVER_RECV) {
      annotation_values_.sr = true;
    }
    if (ann.value() == ZipkinCoreConstants::SERVER_SEND) {
      annotation_values_.ss = true;
    }
  }

  is_populated_ = true;
}

const std::string SpanContext::serializeToString() {
  if (!is_populated_) {
    return "0000000000000000;0000000000000000;0000000000000000";
  }

  std::string s;
  s = traceIdAsHexString() + FIELD_SEPARATOR_ + idAsHexString() + FIELD_SEPARATOR_ +
      parentIdAsHexString();

  if (annotation_values_.cr) {
    s += FIELD_SEPARATOR_ + ZipkinCoreConstants::CLIENT_RECV;
  }
  if (annotation_values_.cs) {
    s += FIELD_SEPARATOR_ + ZipkinCoreConstants::CLIENT_SEND;
  }
  if (annotation_values_.sr) {
    s += FIELD_SEPARATOR_ + ZipkinCoreConstants::SERVER_RECV;
  }
  if (annotation_values_.ss) {
    s += FIELD_SEPARATOR_ + ZipkinCoreConstants::SERVER_SEND;
  }

  return s;
}

void SpanContext::populateFromString(std::string s) {
  // ^([0-9,a-z]{16});([0-9,a-z]{16});([0-9,a-z]{16})((;(cs|sr|cr|ss))*)$
  std::string hex_digit_group = "([0-9,a-z]{16})";
  std::string reg_ex_str =
      "^" + hex_digit_group + FIELD_SEPARATOR_ + hex_digit_group + FIELD_SEPARATOR_ +
      hex_digit_group + "((" + FIELD_SEPARATOR_ + "(" + ZipkinCoreConstants::CLIENT_SEND + "|" +
      ZipkinCoreConstants::SERVER_RECV + "|" + ZipkinCoreConstants::CLIENT_RECV + "|" +
      ZipkinCoreConstants::SERVER_SEND + "))*)$";

  std::regex re(reg_ex_str);
  std::smatch match;

  trace_id_ = parent_id_ = id_ = 0;
  annotation_values_.cs = annotation_values_.cr = annotation_values_.ss = annotation_values_.sr =
      false;

  if (std::regex_search(s, match, re)) {
    // This is a valid string encoding of the context
    trace_id_ = stoull(match.str(1), nullptr, 16);
    id_ = stoull(match.str(2), nullptr, 16);
    parent_id_ = stoull(match.str(3), nullptr, 16);

    if (match.str(4).size() > 0) {
      char* annotation_value_strings = const_cast<char*>(match.str(4).c_str());
      char* annotation_value = strtok(annotation_value_strings, FIELD_SEPARATOR_.c_str());
      while (annotation_value) {
        if (!strcmp(annotation_value, ZipkinCoreConstants::CLIENT_RECV.c_str())) {
          annotation_values_.cr = true;
        } else if (!strcmp(annotation_value, ZipkinCoreConstants::CLIENT_SEND.c_str())) {
          annotation_values_.cs = true;
        } else if (!strcmp(annotation_value, ZipkinCoreConstants::SERVER_RECV.c_str())) {
          annotation_values_.sr = true;
        } else if (!strcmp(annotation_value, ZipkinCoreConstants::SERVER_SEND.c_str())) {
          annotation_values_.ss = true;
        }

        annotation_value = strtok(NULL, FIELD_SEPARATOR_.c_str());
      }
    }

    is_populated_ = true;
  } else {
    is_populated_ = false;
  }
}
} // Zipkin
