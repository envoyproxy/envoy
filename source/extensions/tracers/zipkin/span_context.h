#pragma once

#include <regex>

#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_core_types.h"

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
   */
  SpanContext(const uint64_t trace_id_high, const uint64_t trace_id, const uint64_t id,
              const uint64_t parent_id, bool sampled)
      : trace_id_high_(trace_id_high), trace_id_(trace_id), id_(id), parent_id_(parent_id),
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

private:
  const uint64_t trace_id_high_{0};
  const uint64_t trace_id_{0};
  const uint64_t id_{0};
  const uint64_t parent_id_{0};
  const bool sampled_{false};
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
