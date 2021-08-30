#pragma once

#include "envoy/config/trace/v3/dynamic_ot.pb.h"
#include "envoy/config/trace/v3/dynamic_ot.pb.validate.h"

#include "source/extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

/**
 * Config registration for the dynamic opentracing tracer. @see TracerFactory.
 */
class DynamicOpenTracingTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v3::DynamicOtConfig> {
public:
  DynamicOpenTracingTracerFactory();

private:
  // FactoryBase
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const envoy::config::trace::v3::DynamicOtConfig& configuration,
                          Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
