#include "common/tracing/zipkin/zipkin_core_constants.h"
#include "common/tracing/zipkin/span_context.h"

namespace Zipkin {

const std::string SpanContext::FIELD_SEPARATOR_ = ";";

SpanContext::SpanContext(const Span& span) {
  trace_id_ = span.traceId();
  id_ = span.id();
  parent_id_ = span.parentId();

  for (const Annotation& annotation : span.annotations()) {
    if (annotation.value() == ZipkinCoreConstants::CLIENT_RECV) {
      annotation_values_.cr_ = true;
    }
    if (annotation.value() == ZipkinCoreConstants::CLIENT_SEND) {
      annotation_values_.cs_ = true;
    }
    if (annotation.value() == ZipkinCoreConstants::SERVER_RECV) {
      annotation_values_.sr_ = true;
    }
    if (annotation.value() == ZipkinCoreConstants::SERVER_SEND) {
      annotation_values_.ss_ = true;
    }
  }

  is_populated_ = true;
}

const std::string SpanContext::serializeToString() {
  if (!is_populated_) {
    return "0000000000000000;0000000000000000;0000000000000000";
  }

  std::string result;
  result = traceIdAsHexString() + FIELD_SEPARATOR_ + idAsHexString() + FIELD_SEPARATOR_ +
           parentIdAsHexString();

  if (annotation_values_.cr_) {
    result += FIELD_SEPARATOR_ + ZipkinCoreConstants::CLIENT_RECV;
  }
  if (annotation_values_.cs_) {
    result += FIELD_SEPARATOR_ + ZipkinCoreConstants::CLIENT_SEND;
  }
  if (annotation_values_.sr_) {
    result += FIELD_SEPARATOR_ + ZipkinCoreConstants::SERVER_RECV;
  }
  if (annotation_values_.ss_) {
    result += FIELD_SEPARATOR_ + ZipkinCoreConstants::SERVER_SEND;
  }

  return result;
}

void SpanContext::populateFromString(const std::string& s) {
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
  annotation_values_.cs_ = annotation_values_.cr_ = annotation_values_.ss_ =
      annotation_values_.sr_ = false;

  if (std::regex_search(s, match, re)) {
    // This is a valid string encoding of the context
    trace_id_ = stoull(match.str(1), nullptr, 16);
    id_ = stoull(match.str(2), nullptr, 16);
    parent_id_ = stoull(match.str(3), nullptr, 16);

    std::string matched_annotations = match.str(4);
    if (matched_annotations.size() > 0) {
      char* annotation_value_strings = const_cast<char*>(matched_annotations.c_str());
      char* annotation_value = std::strtok(annotation_value_strings, FIELD_SEPARATOR_.c_str());

      while (annotation_value) {
        if (!std::strcmp(annotation_value, ZipkinCoreConstants::CLIENT_RECV.c_str())) {
          annotation_values_.cr_ = true;
        } else if (!std::strcmp(annotation_value, ZipkinCoreConstants::CLIENT_SEND.c_str())) {
          annotation_values_.cs_ = true;
        } else if (!std::strcmp(annotation_value, ZipkinCoreConstants::SERVER_RECV.c_str())) {
          annotation_values_.sr_ = true;
        } else if (!std::strcmp(annotation_value, ZipkinCoreConstants::SERVER_SEND.c_str())) {
          annotation_values_.ss_ = true;
        }

        annotation_value = std::strtok(NULL, FIELD_SEPARATOR_.c_str());
      }
    }

    is_populated_ = true;
  } else {
    is_populated_ = false;
  }
}
} // Zipkin
