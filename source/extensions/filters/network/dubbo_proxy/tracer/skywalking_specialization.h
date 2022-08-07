#pragma once

#include "envoy/tracing/trace_driver.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

class SkyWalkingSpecialization {
public:
  static void setSpanLayerToRPCFramework(Tracing::SpanPtr& span);
};

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
