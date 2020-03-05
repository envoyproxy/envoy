#pragma once

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/tracing/http_tracer.h"

namespace Envoy {
namespace Tracing {

/**
 * An HttpTracer manager which ensures existence of at most one
 * HttpTracer instance for a given configuration.
 */
class HttpTracerManager {
public:
  virtual ~HttpTracerManager() = default;

  /**
   * Get an existing HttpTracer or create a new one for a given configuration.
   * @param config supplies the configuration for the tracing provider.
   * @return HttpTracerSharedPtr.
   */
  virtual HttpTracerSharedPtr
  getOrCreateHttpTracer(const envoy::config::trace::v3::Tracing_Http* config) PURE;
};

using HttpTracerManagerSharedPtr = std::shared_ptr<HttpTracerManager>;

} // namespace Tracing
} // namespace Envoy
