#include "extensions/tracers/opencensus/config.h"

#include "envoy/registry/registry.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"
#include "extensions/tracers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

OpenCensusTracerFactory::OpenCensusTracerFactory() : FactoryBase(TracerNames::get().OpenCensus) {}

Tracing::HttpTracerPtr OpenCensusTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v2::OpenCensusConfig& proto_config, Server::Instance& server) {
  Tracing::DriverPtr driver = std::make_unique<Driver>(proto_config, server.localInfo());
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(driver), server.localInfo());
}

/**
 * Static registration for the OpenCensus tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(OpenCensusTracerFactory, Server::Configuration::TracerFactory);

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
