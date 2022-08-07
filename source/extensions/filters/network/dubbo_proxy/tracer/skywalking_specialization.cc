#include "source/extensions/filters/network/dubbo_proxy/tracer/skywalking_specialization.h"

#include "source/extensions/tracers/skywalking/tracer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

void SkyWalkingSpecialization::setSpanLayerToRPCFramework(Tracing::SpanPtr& span) {
  try {
    auto& sky_span = dynamic_cast<Tracers::SkyWalking::Span&>(*span);
    sky_span.spanEntity()->setSpanLayer(skywalking::v3::SpanLayer::RPCFramework);
  } catch (const std::bad_cast&) {
  }
}

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
