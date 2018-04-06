#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

/**
 * Config registration for the dynamic opentracing tracer. @see HttpTracerFactory.
 */
class DynamicOpenTracingHttpTracerFactory : public Server::Configuration::HttpTracerFactory {
public:
  // HttpTracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const Json::Object& json_config,
                                          Server::Instance& server) override;
  std::string name() override;
};

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
