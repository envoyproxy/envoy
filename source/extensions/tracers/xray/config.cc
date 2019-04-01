#include "extensions/tracers/xray/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
XRayTracerFactory::XRayTracerFactory() : FactoryBase(TracerNames::get().XRay) {}

Tracing::HttpTracerPtr
XRayTracerFactory::createHttpTracerTyped(const envoy::config::trace::v2::XRayConfig& proto_config,
                                         Server::Instance& server) {
  Tracing::DriverPtr xray_driver =
      std::make_unique<XRay::Driver>(proto_config, server.threadLocal(), server.runtime(),
                                     server.localInfo(), server.random(), server.timeSource());

  return std::make_unique<Tracing::HttpTracerImpl>(std::move(xray_driver), server.localInfo());
}

/**
 * Static registration for the xray tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(XRayTracerFactory, Server::Configuration::TracerFactory);

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
