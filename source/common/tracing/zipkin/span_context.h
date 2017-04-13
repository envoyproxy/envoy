#pragma once

#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_types.h"

namespace Zipkin {

/**
 * This struct identifies which Zipkin annotations are present in the
 * span context (see SpanContext)
 * Each member is a one-bit boolean indicating whether or not the
 * corresponding annotation is present.
 *
 * In particular, the following annotations are tracked by this struct:
 * CS: "Client Send"
 * CR: "Client Receive"
 * SS: "Server Send"
 * SR: "Server Receive"
 */
typedef struct AnnotationIsSet {
  AnnotationIsSet() : cs_(false), cr_(false), ss_(false), sr_(false) {}
  bool cs_ : 1;
  bool cr_ : 1;
  bool ss_ : 1;
  bool sr_ : 1;
} AnnotationIsSet;

/**
 * This class represents the context of a Zipkin span. It embodies the following
 * span characteristics: trace id, span id, parent id, and basic annotations.
 */
class SpanContext {
public:
  /**
   * Default constructor. Creates an empty context.
   */
  SpanContext() : trace_id_(0), id_(0), parent_id_(0), is_initialized_(false) {}

  /**
   * Constructor that creates a context object from the given Zipkin span object.
   *
   * @param span The Zipkin span used to initialize a SpanContext object.
   */
  SpanContext(const Span& span);

  /**
   * Destructor
   */
  virtual ~SpanContext() {}

  /**
   * Serializes the SpanContext object as a string. This encoding of a SpanContext is used
   * as the contents of the x-ot-span-context HTTP header, and allows Envoy to track the
   * relationships among related Zipkin spans.
   *
   * @return a string-encoded SpanContext in the following format:
   *
   * "<16-hex-string trace id>;<16-hex-string span id>;<16-hex-string parent id>;<annotation list>
   *
   * The annotation list, if present, can contain the strings "cs", "cr", "ss", "sr", depending on
   * which annotations are present. The semi-colon character is used as the separator for the
   * annotation list.
   *
   * Example of a returned string corresponding to a span with the SR annotation:
   * "25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;sr"
   *
   * Example of a returned string corresponding to a span with no annotations:
   * "25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c"
   */
  const std::string serializeToString();

  /**
   * Initializes a SpanContext object based on the given string.
   *
   * @param span_context_str The string-encoding of a SpanContext in the same format produced by the
   * method serializeToString().
   */
  void populateFromString(const std::string& span_context_str);

  /**
   * @return the span id as an integer
   */
  uint64_t id() const { return id_; }

  /**
   * @return the span id as a 16-character hexadecimal string.
   */
  std::string idAsHexString() const { return Util::uint64ToBase16(id_); }

  /**
   * @return the span's parent id as an integer.
   */
  uint64_t parent_id() const { return parent_id_; }

  /**
   * @return the parent id as a 16-character hexadecimal string.
   */
  std::string parentIdAsHexString() const { return Util::uint64ToBase16(parent_id_); }

  /**
   * @return the trace id as an integer.
   */
  uint64_t trace_id() const { return trace_id_; }

  /**
   * @return the trace id as a 16-character hexadecimal string.
   */
  std::string traceIdAsHexString() const { return Util::uint64ToBase16(trace_id_); }

  /**
   * @return a struct indicating which annotations are present in the span.
   */
  AnnotationIsSet isSetAnnotation() const { return annotation_values_; }

private:
  static const std::string FIELD_SEPARATOR_;
  static const std::string UNITIALIZED_SPAN_CONTEXT_;
  static const std::string HEX_DIGIT_GROUP_REGEX_STR_;

  /**
   * @return a string with a regular expression to match a valid string-serialized span context.
   *
   * Note that a function is needed because we cannot concatenate static strings from
   * different compilation units at initialization time (the initialization order is not
   * guaranteed). In this case, the compilation units are ZipkinCoreConstants and SpanContext.
   */
  static const std::string& SPAN_CONTEXT_REGEX_STR();

  /**
   * @return a regex to match a valid string-serialization of a span context.
   *
   * Note that a function is needed because the string used to build the regex
   * cannot be initialized statically.
   */
  static const std::regex& SPAN_CONTEXT_REGEX();

  uint64_t trace_id_;
  uint64_t id_;
  uint64_t parent_id_;
  AnnotationIsSet annotation_values_;
  bool is_initialized_;
};
} // Zipkin
