#include "source/extensions/tracers/opencensus/config.h"

#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opencensus/opencensus_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

OpenCensusTracerFactory::OpenCensusTracerFactory() : FactoryBase("envoy.tracers.opencensus") {}

Tracing::DriverSharedPtr OpenCensusTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::OpenCensusConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  // Since OpenCensus can only support a single tracing configuration per entire process,
  // we need to make sure that it is configured at most once.
  if (driver_) {
    if (Envoy::Protobuf::util::MessageDifferencer::Equals(config_, proto_config)) {
      return driver_;
    } else {
      throw EnvoyException("Opencensus has already been configured with a different config.");
    }
  }

  driver_ = std::make_shared<Driver>(proto_config, context.serverFactoryContext());
  config_ = proto_config;
  return driver_;
}

/**
 * Static registration for the OpenCensus tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(OpenCensusTracerFactory, Server::Configuration::TracerFactory);

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
