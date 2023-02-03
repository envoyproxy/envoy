#pragma once

#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/tracing/tracer.h"

namespace Envoy {
namespace Tracing {

/**
 * An Tracer manager which ensures existence of at most one Tracer instance
 * for a given configuration.
 */
class TracerManager {
public:
  virtual ~TracerManager() = default;

  /**
   * Get an existing Tracer or create a new one for a given configuration.
   * @param config supplies the configuration for the tracing provider.
   * @return TracerSharedPtr.
   */
  virtual TracerSharedPtr
  getOrCreateTracer(const envoy::config::trace::v3::Tracing_Http* config) PURE;
};

using TracerManagerSharedPtr = std::shared_ptr<TracerManager>;

} // namespace Tracing
} // namespace Envoy
