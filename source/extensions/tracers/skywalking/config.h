#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/config/trace/v3/skywalking.pb.validate.h"

#include "source/extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

/**
 * Config registration for the SkyWalking tracer. @see TracerFactory.
 */
class SkyWalkingTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v3::SkyWalkingConfig> {
public:
  SkyWalkingTracerFactory();

private:
  // FactoryBase
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
                          Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
