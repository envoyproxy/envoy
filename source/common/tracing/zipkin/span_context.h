#pragma once

#include <regex>

#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"
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
  SpanContext() : trace_id_(0), id_(0), parent_id_(0), is_initialized_(false), sampled_(false) {}

  /**
   * Constructor that creates a context object from the supplied trace, span and
   * parent ids, and sampled flag.
   *
   * @param trace_id The trace id.
   * @param id The span id.
   * @param parent_id The parent id.
   * @param sampled The sampled flag.
   */
  SpanContext(const uint64_t trace_id, const uint64_t id, const uint64_t parent_id, bool sampled)
      : trace_id_(trace_id), id_(id), parent_id_(parent_id), is_initialized_(true),
        sampled_(sampled) {}

  /**
   * Constructor that creates a context object from the given Zipkin span object.
   *
   * @param span The Zipkin span used to initialize a SpanContext object.
   */
  SpanContext(const Span& span);

  /**
   * @return the span id as an integer
   */
  uint64_t id() const { return id_; }

  /**
   * @return the span's parent id as an integer.
   */
  uint64_t parent_id() const { return parent_id_; }

  /**
   * @return the trace id as an integer.
   */
  uint64_t trace_id() const { return trace_id_; }

  /**
   * @return the sampled flag.
   */
  bool sampled() const { return sampled_; }

private:
  uint64_t trace_id_;
  uint64_t id_;
  uint64_t parent_id_;
  bool is_initialized_;
  bool sampled_;
};
} // namespace Zipkin
} // namespace Envoy
