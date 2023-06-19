#pragma once

#include <regex>

#include "source/extensions/tracers/zipkin/util.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"
#include "source/extensions/tracers/zipkin/zipkin_core_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
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
  SpanContext() = default;

  /**
   * Constructor that creates a context object from the supplied trace, span and
   * parent ids, and sampled flag.
   *
   * @param trace_id_high The high 64 bits of the trace id.
   * @param trace_id The low 64 bits of the trace id.
   * @param id The span id.
   * @param parent_id The parent id.
   * @param sampled The sampled flag.
   * @param inner_context If this context is created base on the inner span.
   */
  SpanContext(const uint64_t trace_id_high, const uint64_t trace_id, const uint64_t id,
              const uint64_t parent_id, bool sampled, bool inner_context = false)
      : trace_id_high_(trace_id_high), trace_id_(trace_id), id_(id), parent_id_(parent_id),
        sampled_(sampled), inner_context_(inner_context) {}

  /**
   * Constructor that creates a context object from the given Zipkin span object.
   *
   * @param span The Zipkin span used to initialize a SpanContext object.
   * @param inner_context If this context is created base on the inner span.
   */
  SpanContext(const Span& span, bool inner_context = true);

  /**
   * @return the span id as an integer
   */
  uint64_t id() const { return id_; }

  /**
   * @return the span's parent id as an integer.
   */
  uint64_t parentId() const { return parent_id_; }

  /**
   * @return the high 64 bits of the trace id as an integer.
   */
  uint64_t traceIdHigh() const { return trace_id_high_; }

  /**
   * @return the low 64 bits of the trace id as an integer.
   */
  uint64_t traceId() const { return trace_id_; }

  /**
   * @return whether using 128 bit trace id.
   */
  bool is128BitTraceId() const { return trace_id_high_ != 0; }

  /**
   * @return the sampled flag.
   */
  bool sampled() const { return sampled_; }

  /**
   * @return the inner context flag.
   */
  bool innerContext() const { return inner_context_; }

private:
  const uint64_t trace_id_high_{0};
  const uint64_t trace_id_{0};
  const uint64_t id_{0};
  const uint64_t parent_id_{0};
  const bool sampled_{false};
  const bool inner_context_{false};
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
