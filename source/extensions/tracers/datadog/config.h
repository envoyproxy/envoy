#pragma once

#include <string>

#include "envoy/config/trace/v2/trace.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Config registration for the Datadog tracer. @see TracerFactory.
 */
class DatadogTracerFactory : public Common::FactoryBase<envoy::config::trace::v2::DatadogConfig> {
public:
  DatadogTracerFactory();

private:
  // FactoryBase
  Tracing::HttpTracerPtr
  createHttpTracerTyped(const envoy::config::trace::v2::DatadogConfig& proto_config,
                        Server::Instance& server) override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
