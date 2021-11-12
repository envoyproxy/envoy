#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_context.h"
#include "envoy/tracing/trace_reason.h"

namespace Envoy {
namespace Tracing {

class Span;
using SpanPtr = std::unique_ptr<Span>;

constexpr uint32_t DefaultMaxPathTagLength = 256;

enum class OperationName { Ingress, Egress };

/**
 * The context for the custom tag to obtain the tag value.
 */
struct CustomTagContext {
  const TraceContext* trace_context;
  const StreamInfo::StreamInfo& stream_info;
};

/**
 * Tracing custom tag, with tag name and how it would be applied to the span.
 */
class CustomTag {
public:
  virtual ~CustomTag() = default;

  /**
   * @return the tag name view.
   */
  virtual absl::string_view tag() const PURE;

  /**
   * The way how to apply the custom tag to the span,
   * generally obtain the tag value from the context and attached it to the span.
   * @param span the active span.
   * @param ctx the custom tag context.
   */
  virtual void apply(Span& span, const CustomTagContext& ctx) const PURE;
};

using CustomTagConstSharedPtr = std::shared_ptr<const CustomTag>;
using CustomTagMap = absl::flat_hash_map<std::string, CustomTagConstSharedPtr>;

/**
 * Tracing configuration, it carries additional data needed to populate the span.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * @return operation name for tracing, e.g., ingress.
   */
  virtual OperationName operationName() const PURE;

  /**
   * @return custom tags to be attached to the active span.
   */
  virtual const CustomTagMap* customTags() const PURE;

  /**
   * @return true if spans should be annotated with more detailed information.
   */
  virtual bool verbose() const PURE;

  /**
   * @return the maximum length allowed for paths in the extracted HttpUrl tag. This is only used
   * for HTTP protocol tracing.
   */
  virtual uint32_t maxPathTagLength() const PURE;
};

/**
 * Basic abstraction for span.
 */
class Span {
public:
  virtual ~Span() = default;

  /**
   * Set the operation name.
   * @param operation the operation name
   */
  virtual void setOperation(absl::string_view operation) PURE;

  /**
   * Attach metadata to a Span, to be handled in an implementation-dependent fashion.
   * @param name the name of the tag
   * @param value the value to associate with the tag
   */
  virtual void setTag(absl::string_view name, absl::string_view value) PURE;

  /**
   * Record an event associated with a span, to be handled in an implementation-dependent fashion.
   * @param timestamp the time of the event.
   * @param event the name of the event.
   */
  virtual void log(SystemTime timestamp, const std::string& event) PURE;

  /**
   * Capture the final duration for this Span and carry out any work necessary to complete it.
   * Once this method is called, the Span may be safely discarded.
   */
  virtual void finishSpan() PURE;

  /**
   * Mutate the provided headers with the context necessary to propagate this
   * (implementation-specific) trace.
   * @param request_headers the headers to which propagation context will be added
   */
  virtual void injectContext(TraceContext& trace_conext) PURE;

  /**
   * Create and start a child Span, with this Span as its parent in the trace.
   * @param config the tracing configuration
   * @param name operation name captured by the spawned child
   * @param start_time initial start time for the operation captured by the child
   */
  virtual SpanPtr spawnChild(const Config& config, const std::string& name,
                             SystemTime start_time) PURE;

  /**
   * This method overrides any previous sampling decision associated with the trace instance.
   * If the sampled parameter is false, this span and any subsequent child spans
   * are not reported to the tracing system.
   * @param sampled whether the span and any subsequent child spans should be sampled
   */
  virtual void setSampled(bool sampled) PURE;

  /**
   * Retrieve a key's value from the span's baggage.
   * This baggage data could've been set by this span or any parent spans.
   * @param key baggage key
   * @return the baggage's value for the given input key
   */
  virtual std::string getBaggage(absl::string_view key) PURE;

  /**
   * Set a key/value pair in the current span's baggage.
   * All subsequent child spans will have access to this baggage.
   * @param key baggage key
   * @param key baggage value
   */
  virtual void setBaggage(absl::string_view key, absl::string_view value) PURE;

  /**
   * Retrieve the trace ID associated with this span.
   * The trace id may be generated for this span, propagated by parent spans, or
   * not created yet.
   * @return trace ID as a hex string
   */
  virtual std::string getTraceIdAsHex() const PURE;
};

/**
 * Tracing driver is responsible for span creation.
 */
class Driver {
public:
  virtual ~Driver() = default;

  /**
   * Start driver specific span.
   */
  virtual SpanPtr startSpan(const Config& config, TraceContext& trace_conext,
                            const std::string& operation_name, SystemTime start_time,
                            const Tracing::Decision tracing_decision) PURE;
};

using DriverPtr = std::unique_ptr<Driver>;
using DriverSharedPtr = std::shared_ptr<Driver>;

} // namespace Tracing
} // namespace Envoy
