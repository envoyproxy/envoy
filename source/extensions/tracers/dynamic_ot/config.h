#pragma once

#include "envoy/config/trace/v2/trace.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

/**
 * Config registration for the dynamic opentracing tracer. @see TracerFactory.
 */
class DynamicOpenTracingTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v2::DynamicOtConfig> {
public:
  DynamicOpenTracingTracerFactory();

private:
  // FactoryBase
  Tracing::HttpTracerPtr
  createHttpTracerTyped(const envoy::config::trace::v2::DynamicOtConfig& configuration,
                        Server::Instance& server) override;
};

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
