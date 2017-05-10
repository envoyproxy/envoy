#include "common/tracing/zipkin/span_context.h"

#include "common/common/utility.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Zipkin {

namespace {
// The functions below are needed due to the "C++ static initialization order fiasco."

/**
 * @return String that separates the span-context fields in its string-serialized form.
 */
static const std::string& fieldSeparator() {
  static const std::string* field_separator = new std::string(";");

  return *field_separator;
}

/**
 * @return String value corresponding to an empty span context.
 */
static const std::string& unitializedSpanContext() {
  static const std::string* unitialized_span_context =
      new std::string("0000000000000000" + fieldSeparator() + "0000000000000000" +
                      fieldSeparator() + "0000000000000000");

  return *unitialized_span_context;
}

/**
 * @return String with regular expression to match a 16-digit hexadecimal number.
 */
static const std::string& hexDigitGroupRegexStr() {
  static const std::string* hex_digit_group_regex_str = new std::string("([0-9,a-z]{16})");

  return *hex_digit_group_regex_str;
}

/**
 * @return a string with a regular expression to match a valid string-serialized span context.
 *
 * Note that a function is needed because we cannot concatenate static strings from
 * different compilation units at initialization time (the initialization order is not
 * guaranteed). In this case, the compilation units are ZipkinCoreConstants and SpanContext.
 */
static const std::string& spanContextRegexStr() {
  // ^([0-9,a-z]{16});([0-9,a-z]{16});([0-9,a-z]{16})((;(cs|sr|cr|ss))*)$
  static const std::string* span_context_regex_str = new std::string(
      "^" + hexDigitGroupRegexStr() + fieldSeparator() + hexDigitGroupRegexStr() +
      fieldSeparator() + hexDigitGroupRegexStr() + "((" + fieldSeparator() + "(" +
      ZipkinCoreConstants::get().CLIENT_SEND + "|" + ZipkinCoreConstants::get().SERVER_RECV + "|" +
      ZipkinCoreConstants::get().CLIENT_RECV + "|" + ZipkinCoreConstants::get().SERVER_SEND +
      "))*)$");

  return *span_context_regex_str;
}

/**
 * @return a regex to match a valid string-serialization of a span context.
 *
 * Note that a function is needed because the string used to build the regex
 * cannot be initialized statically.
 */
static const std::regex& spanContextRegex() {
  static const std::regex* span_context_regex = new std::regex(spanContextRegexStr());

  return *span_context_regex;
}
} // namespace

SpanContext::SpanContext(const Span& span) {
  trace_id_ = span.traceId();
  id_ = span.id();
  parent_id_ = span.isSetParentId() ? span.parentId() : 0;

  for (const Annotation& annotation : span.annotations()) {
    if (annotation.value() == ZipkinCoreConstants::get().CLIENT_RECV) {
      annotation_values_.cr_ = true;
    } else if (annotation.value() == ZipkinCoreConstants::get().CLIENT_SEND) {
      annotation_values_.cs_ = true;
    } else if (annotation.value() == ZipkinCoreConstants::get().SERVER_RECV) {
      annotation_values_.sr_ = true;
    } else if (annotation.value() == ZipkinCoreConstants::get().SERVER_SEND) {
      annotation_values_.ss_ = true;
    }
  }

  is_initialized_ = true;
}

const std::string SpanContext::serializeToString() {
  if (!is_initialized_) {
    return unitializedSpanContext();
  }

  std::string result;
  result = traceIdAsHexString() + fieldSeparator() + idAsHexString() + fieldSeparator() +
           parentIdAsHexString();

  if (annotation_values_.cr_) {
    result += fieldSeparator() + ZipkinCoreConstants::get().CLIENT_RECV;
  }
  if (annotation_values_.cs_) {
    result += fieldSeparator() + ZipkinCoreConstants::get().CLIENT_SEND;
  }
  if (annotation_values_.sr_) {
    result += fieldSeparator() + ZipkinCoreConstants::get().SERVER_RECV;
  }
  if (annotation_values_.ss_) {
    result += fieldSeparator() + ZipkinCoreConstants::get().SERVER_SEND;
  }

  return result;
}

void SpanContext::populateFromString(const std::string& span_context_str) {
  std::smatch match;

  trace_id_ = parent_id_ = id_ = 0;
  annotation_values_.cs_ = annotation_values_.cr_ = annotation_values_.ss_ =
      annotation_values_.sr_ = false;

  if (std::regex_search(span_context_str, match, spanContextRegex())) {
    // This is a valid string encoding of the context
    trace_id_ = std::stoull(match.str(1), nullptr, 16);
    id_ = std::stoull(match.str(2), nullptr, 16);
    parent_id_ = std::stoull(match.str(3), nullptr, 16);

    std::string matched_annotations = match.str(4);
    if (matched_annotations.size() > 0) {
      std::vector<std::string> annotation_value_strings =
          StringUtil::split(matched_annotations, fieldSeparator());
      for (const std::string& annotation_value : annotation_value_strings) {
        if (annotation_value == ZipkinCoreConstants::get().CLIENT_RECV) {
          annotation_values_.cr_ = true;
        } else if (annotation_value == ZipkinCoreConstants::get().CLIENT_SEND) {
          annotation_values_.cs_ = true;
        } else if (annotation_value == ZipkinCoreConstants::get().SERVER_RECV) {
          annotation_values_.sr_ = true;
        } else if (annotation_value == ZipkinCoreConstants::get().SERVER_SEND) {
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
} // Envoy
