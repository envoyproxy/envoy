#pragma once

#include <regex>

#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_types.h"

namespace Envoy {
namespace Zipkin {

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
   * Constructor that creates a context object from the supplied trace, span
   * and parent ids.
   *
   * @param trace_id The trace id.
   * @param id The span id.
   * @param parent_id The parent id.
   */
  SpanContext(const uint64_t trace_id, const uint64_t id, const uint64_t parent_id)
      : trace_id_(trace_id), id_(id), parent_id_(parent_id), is_initialized_(true) {}

  /**
   * Constructor that creates a context object from the given Zipkin span object.
   *
   * @param span The Zipkin span used to initialize a SpanContext object.
   */
  SpanContext(const Span& span);

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
  std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

  /**
   * @return the span's parent id as an integer.
   */
  uint64_t parent_id() const { return parent_id_; }

  /**
   * @return the parent id as a 16-character hexadecimal string.
   */
  std::string parentIdAsHexString() const { return Hex::uint64ToHex(parent_id_); }

  /**
   * @return the trace id as an integer.
   */
  uint64_t trace_id() const { return trace_id_; }

  /**
   * @return the trace id as a 16-character hexadecimal string.
   */
  std::string traceIdAsHexString() const { return Hex::uint64ToHex(trace_id_); }

private:
  uint64_t trace_id_;
  uint64_t id_;
  uint64_t parent_id_;
  bool is_initialized_;
};
} // namespace Zipkin
} // namespace Envoy
