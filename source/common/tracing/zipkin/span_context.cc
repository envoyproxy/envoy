#include "common/tracing/zipkin/span_context.h"

#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Zipkin {

namespace {
// The functions below are needed due to the "C++ static initialization order fiasco."

/**
 * @return String that separates the span-context fields in its string-serialized form.
 */
static const std::string& fieldSeparator() { CONSTRUCT_ON_FIRST_USE(std::string, ";") }

/**
 * @return String value corresponding to an empty span context.
 */
static const std::string& unitializedSpanContext() {
  CONSTRUCT_ON_FIRST_USE(std::string, "0000000000000000" + fieldSeparator() + "0000000000000000" +
                                          fieldSeparator() + "0000000000000000");
}

/**
 * @return String with regular expression to match a 16-digit hexadecimal number.
 */
static const std::string& hexDigitGroupRegexStr() {
  CONSTRUCT_ON_FIRST_USE(std::string, "([0-9,a-z]{16})");
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
  CONSTRUCT_ON_FIRST_USE(std::string, "^" + hexDigitGroupRegexStr() + fieldSeparator() +
                                          hexDigitGroupRegexStr() + fieldSeparator() +
                                          hexDigitGroupRegexStr() + "((" + fieldSeparator() + "(" +
                                          ZipkinCoreConstants::get().CLIENT_SEND + "|" +
                                          ZipkinCoreConstants::get().SERVER_RECV + "|" +
                                          ZipkinCoreConstants::get().CLIENT_RECV + "|" +
                                          ZipkinCoreConstants::get().SERVER_SEND + "))*)$");
}

/**
 * @return a regex to match a valid string-serialization of a span context.
 *
 * Note that a function is needed because the string used to build the regex
 * cannot be initialized statically.
 */
static const std::regex& spanContextRegex() {
  CONSTRUCT_ON_FIRST_USE(std::regex, spanContextRegexStr());
}
} // namespace

SpanContext::SpanContext(const Span& span) {
  trace_id_ = span.traceId();
  id_ = span.id();
  parent_id_ = span.isSetParentId() ? span.parentId() : 0;

  is_initialized_ = true;
}

const std::string SpanContext::serializeToString() {
  if (!is_initialized_) {
    return unitializedSpanContext();
  }

  std::string result;
  result = traceIdAsHexString() + fieldSeparator() + idAsHexString() + fieldSeparator() +
           parentIdAsHexString();

  return result;
}

void SpanContext::populateFromString(const std::string& span_context_str) {
  std::smatch match;

  trace_id_ = parent_id_ = id_ = 0;

  if (std::regex_search(span_context_str, match, spanContextRegex())) {
    // This is a valid string encoding of the context
    trace_id_ = std::stoull(match.str(1), nullptr, 16);
    id_ = std::stoull(match.str(2), nullptr, 16);
    parent_id_ = std::stoull(match.str(3), nullptr, 16);

    is_initialized_ = true;
  } else {
    is_initialized_ = false;
  }
}
} // namespace Zipkin
} // namespace Envoy
