#pragma once

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
  const std::string LIGHTSTEP = "envoy.lightstep";
  // Zipkin tracer
  const std::string ZIPKIN = "envoy.zipkin";
  // Dynamic tracer
  const std::string DYNAMIC_OT = "envoy.dynamic.ot";
};

typedef ConstSingleton<TracerNameValues> TracerNames;

} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
