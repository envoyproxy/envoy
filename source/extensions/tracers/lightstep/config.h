#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

/**
 * Config registration for the lightstep tracer. @see TracerFactory.
 */
class LightstepTracerFactory : public Server::Configuration::TracerFactory {
public:
  // TracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const Json::Object& json_config,
                                          Server::Instance& server) override;

  std::string name() override;
};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
