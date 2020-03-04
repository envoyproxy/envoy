#pragma once

#include "envoy/config/trace/v3/xray.pb.h"
#include "envoy/config/trace/v3/xray.pb.validate.h"

#include "common/common/logger.h"

#include "extensions/tracers/common/factory_base.h"

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
  Tracing::HttpTracerPtr
  createHttpTracerTyped(const envoy::config::trace::v3::XRayConfig& proto_config,
                        Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
