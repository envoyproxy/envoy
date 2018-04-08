#pragma once

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Config registration for the zipkin tracer. @see TracerFactory.
 */
class ZipkinTracerFactory : public Server::Configuration::TracerFactory {
public:
  // TracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const Json::Object& json_config,
                                          Server::Instance& server) override;
  std::string name() override;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
