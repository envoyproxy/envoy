#pragma once

#include "envoy/config/trace/v3/xray.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * Config registration for the XRay tracer. @see TracerFactory.
 */
class XRayTracerFactory : public Common::FactoryBase<envoy::config::trace::v3::XRayConfig>,
                          Logger::Loggable<Logger::Id::tracing> {
public:
  XRayTracerFactory();

private:
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const envoy::config::trace::v3::XRayConfig& proto_config,
                          Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
