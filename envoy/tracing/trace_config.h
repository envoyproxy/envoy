#pragma once

#include "envoy/tracing/custom_tag.h"

namespace Envoy {
namespace Tracing {

constexpr uint32_t DefaultMaxPathTagLength = 256;

enum class OperationName { Ingress, Egress };

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
   * @return create separated child span for upstream request if true.
   */
  virtual bool spawnUpstreamSpan() const PURE;

  /**
   * @return modify the span. For example, set custom tags from configuration or
   * make other modifications.
   * This method MUST be called at most ONLY once per span before the span is
   * finished.
   * @param span the span to modify.
   * @param upstream_span true if the span is an upstream span that created for outgoing request.
   */
  virtual void modifySpan(Span& span, bool upstream_span) const PURE;

  /**
   * @return true if spans should be annotated with more detailed information.
   */
  virtual bool verbose() const PURE;

  /**
   * @return the maximum length allowed for paths in the extracted HttpUrl tag. This is only used
   * for HTTP protocol tracing.
   */
  virtual uint32_t maxPathTagLength() const PURE;

  /**
   * @return true if trace context propagation should be disabled. When true, trace context headers
   * (e.g., traceparent, tracestate, X-B3-* headers) will not be injected when proxying requests
   * to upstreams. Span reporting still occurs; only context propagation is disabled.
   */
  virtual bool noContextPropagation() const PURE;
};

} // namespace Tracing
} // namespace Envoy
