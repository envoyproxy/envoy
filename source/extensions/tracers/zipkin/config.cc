#include "extensions/tracers/zipkin/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/zipkin/zipkin_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

Tracing::HttpTracerPtr ZipkinTracerFactory::createHttpTracer(const Json::Object& json_config,
                                                             Server::Instance& server) {

  Envoy::Runtime::RandomGenerator& rand = server.random();

  Tracing::DriverPtr zipkin_driver(new Zipkin::Driver(json_config, server.clusterManager(),
                                                      server.stats(), server.threadLocal(),
                                                      server.runtime(), server.localInfo(), rand));

  return Tracing::HttpTracerPtr(
      new Tracing::HttpTracerImpl(std::move(zipkin_driver), server.localInfo()));
}

std::string ZipkinTracerFactory::name() { return TracerNames::get().Zipkin; }

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<ZipkinTracerFactory, Server::Configuration::TracerFactory>
    register_;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
