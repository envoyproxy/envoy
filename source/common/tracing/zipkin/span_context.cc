#include "common/tracing/zipkin/span_context.h"

#include "common/common/utility.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

namespace Zipkin {

// String that separates the span-context fields in its string-serialized form.
const std::string SpanContext::FIELD_SEPARATOR_ = ";";

// String value corresponding to an empty span context.
const std::string SpanContext::UNITIALIZED_SPAN_CONTEXT_ = "0000000000000000" + FIELD_SEPARATOR_ +
                                                           "0000000000000000" + FIELD_SEPARATOR_ +
                                                           "0000000000000000";

// String with regular expression to match a 16-digit hexadecimal number.
const std::string SpanContext::HEX_DIGIT_GROUP_REGEX_STR_ = "([0-9,a-z]{16})";

// The static functions below are needed due to C++ inability to safely concatenate static strings
// belonging to different compilation units (the initialization order is not guaranteed).

const std::string& SpanContext::SPAN_CONTEXT_REGEX_STR() {
  // ^([0-9,a-z]{16});([0-9,a-z]{16});([0-9,a-z]{16})((;(cs|sr|cr|ss))*)$
  static const std::string* span_context_regex_str = new std::string(
      "^" + HEX_DIGIT_GROUP_REGEX_STR_ + FIELD_SEPARATOR_ + HEX_DIGIT_GROUP_REGEX_STR_ +
      FIELD_SEPARATOR_ + HEX_DIGIT_GROUP_REGEX_STR_ + "((" + FIELD_SEPARATOR_ + "(" +
      ZipkinCoreConstants::CLIENT_SEND + "|" + ZipkinCoreConstants::SERVER_RECV + "|" +
      ZipkinCoreConstants::CLIENT_RECV + "|" + ZipkinCoreConstants::SERVER_SEND + "))*)$");

  return *span_context_regex_str;
}

const std::regex& SpanContext::SPAN_CONTEXT_REGEX() {
  static const std::regex span_context_regex(SPAN_CONTEXT_REGEX_STR());

  return span_context_regex;
}

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

  is_initialized_ = true;
}

const std::string SpanContext::serializeToString() {
  if (!is_initialized_) {
    return UNITIALIZED_SPAN_CONTEXT_;
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

void SpanContext::populateFromString(const std::string& span_context_str) {
  std::smatch match;

  trace_id_ = parent_id_ = id_ = 0;
  annotation_values_.cs_ = annotation_values_.cr_ = annotation_values_.ss_ =
      annotation_values_.sr_ = false;

  if (std::regex_search(span_context_str, match, SPAN_CONTEXT_REGEX())) {
    // This is a valid string encoding of the context
    trace_id_ = stoull(match.str(1), nullptr, 16);
    id_ = stoull(match.str(2), nullptr, 16);
    parent_id_ = stoull(match.str(3), nullptr, 16);

    std::string matched_annotations = match.str(4);
    if (matched_annotations.size() > 0) {
      std::vector<std::string> annotation_value_strings =
          StringUtil::split(matched_annotations, FIELD_SEPARATOR_);
      for (const std::string& annotation_value : annotation_value_strings) {
        if (annotation_value == ZipkinCoreConstants::CLIENT_RECV) {
          annotation_values_.cr_ = true;
        } else if (annotation_value == ZipkinCoreConstants::CLIENT_SEND) {
          annotation_values_.cs_ = true;
        } else if (annotation_value == ZipkinCoreConstants::SERVER_RECV) {
          annotation_values_.sr_ = true;
        } else if (annotation_value == ZipkinCoreConstants::SERVER_SEND) {
          annotation_values_.ss_ = true;
        }
      }
    }

    is_initialized_ = true;
  } else {
    is_initialized_ = false;
  }
}
} // Zipkin
