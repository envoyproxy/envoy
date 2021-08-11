#pragma once

#include "envoy/config/trace/v3/lightstep.pb.h"
#include "envoy/config/trace/v3/lightstep.pb.validate.h"

#include "source/extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

/**
 * Config registration for the lightstep tracer. @see TracerFactory.
 */
class LightstepTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v3::LightstepConfig> {
public:
  LightstepTracerFactory();

private:
  // FactoryBase
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const envoy::config::trace::v3::LightstepConfig& proto_config,
                          Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
