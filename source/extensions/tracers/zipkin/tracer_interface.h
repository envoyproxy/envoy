#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/trace/v3/zipkin.pb.h"
#include "envoy/tracing/trace_config.h"

#include "source/extensions/tracers/zipkin/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

using TraceContextOption = envoy::config::trace::v3::ZipkinConfig::TraceContextOption;

class Span;
using SpanPtr = std::unique_ptr<Span>;

/**
 * Abstract class that delegates to users of the Tracer class the responsibility
 * of "reporting" a Zipkin span that has ended its life cycle. "Reporting" can mean that the
 * span will be sent to out to Zipkin, or buffered so that it can be sent out later.
 */
class Reporter {
public:
  /**
   * Destructor.
   */
  virtual ~Reporter() = default;

  /**
   * Method that a concrete Reporter class must implement to handle finished spans.
   * For example, a span-buffer management policy could be implemented.
   *
   * This method is invoked by the Span object when its finish() method is called.
   *
   * @param span The span that needs action.
   */
  virtual void reportSpan(Span&& span) PURE;
};

using ReporterPtr = std::unique_ptr<Reporter>;

/**
 * This interface must be observed by a Zipkin tracer.
 */
class TracerInterface : public Reporter {
public:
  /**
   * Creates a "root" Zipkin span.
   *
   * @param config The tracing configuration
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   * @return SpanPtr The root span.
   */
  virtual SpanPtr startSpan(const Tracing::Config&, const std::string& span_name,
                            SystemTime timestamp) PURE;

  /**
   * Depending on the given context, creates either a "child" or a "shared-context" Zipkin span.
   *
   * @param config The tracing configuration
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   * @param previous_context The context of the span preceding the one to be created.
   * @return SpanPtr The child span.
   */
  virtual SpanPtr startSpan(const Tracing::Config&, const std::string& span_name,
                            SystemTime timestamp, const SpanContext& previous_context) PURE;

  /**
   * Gets the current trace context option for header injection behavior.
   * @return The current trace context option.
   */
  virtual TraceContextOption traceContextOption() const PURE;
};

/**
 * Buffered pending spans serializer.
 */
class Serializer {
public:
  virtual ~Serializer() = default;

  /**
   * Serialize buffered pending spans.
   *
   * @return std::string serialized buffered pending spans.
   */
  virtual std::string serialize(const std::vector<Span>& spans) PURE;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
