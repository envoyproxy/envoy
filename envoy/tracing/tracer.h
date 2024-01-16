#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/tracing/trace_context.h"
#include "envoy/tracing/trace_driver.h"
#include "envoy/tracing/trace_reason.h"

namespace Envoy {
namespace Tracing {

/**
 * Tracer is responsible for handling traces and delegate actions to the
 * corresponding drivers.
 */
class Tracer {
public:
  virtual ~Tracer() = default;

  virtual SpanPtr startSpan(const Config& config, TraceContext& trace_context,
                            const StreamInfo::StreamInfo& stream_info,
                            Tracing::Decision tracing_decision) PURE;
};

using TracerSharedPtr = std::shared_ptr<Tracer>;

} // namespace Tracing
} // namespace Envoy
