#pragma once

#include "envoy/config/trace/v3alpha/trace.pb.h"
#include "envoy/config/trace/v3alpha/trace.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

/**
 * Config registration for the lightstep tracer. @see TracerFactory.
 */
class LightstepTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v3alpha::LightstepConfig> {
public:
  LightstepTracerFactory();

private:
  // FactoryBase
  Tracing::HttpTracerPtr
  createHttpTracerTyped(const envoy::config::trace::v3alpha::LightstepConfig& proto_config,
                        Server::Instance& server) override;
};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
