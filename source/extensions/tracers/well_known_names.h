#pragma once
#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {

/**
 * Well-known tracer names.
 * NOTE: New tracers should use the well known name: envoy.tracers.name.
 */
class TracerNameValues {
public:
  // Lightstep tracer
  const std::string Lightstep = "envoy.lightstep";
  // Zipkin tracer
  const std::string Zipkin = "envoy.zipkin";
  // Dynamic tracer
  const std::string DynamicOt = "envoy.dynamic.ot";
  // Datadog tracer
  const std::string Datadog = "envoy.tracers.datadog";
  // OpenCensus tracer
  const std::string OpenCensus = "envoy.tracers.opencensus";
  // AWS XRay tracer
  const std::string XRay = "envoy.tracers.xray";
};

using TracerNames = ConstSingleton<TracerNameValues>;

} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
