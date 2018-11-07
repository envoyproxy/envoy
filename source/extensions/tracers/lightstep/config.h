#pragma once

#include "envoy/config/trace/v2/trace.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

/**
 * Config registration for the lightstep tracer. @see TracerFactory.
 */
class LightstepTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v2::LightstepConfig> {
public:
  LightstepTracerFactory();

private:
  // FactoryBase
  Tracing::HttpTracerPtr
  createHttpTracerTyped(const envoy::config::trace::v2::LightstepConfig& proto_config,
                        Server::Instance& server) override;
};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
