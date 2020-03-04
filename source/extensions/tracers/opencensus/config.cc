#include "extensions/tracers/opencensus/config.h"

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"
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
    const envoy::config::trace::v3::OpenCensusConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  Tracing::DriverPtr driver =
      std::make_unique<Driver>(proto_config, context.serverFactoryContext().localInfo(),
                               context.serverFactoryContext().api());
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the OpenCensus tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(OpenCensusTracerFactory, Server::Configuration::TracerFactory);

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
