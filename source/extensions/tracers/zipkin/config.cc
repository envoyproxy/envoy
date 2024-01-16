#include "source/extensions/tracers/zipkin/config.h"

#include "envoy/config/trace/v3/zipkin.pb.h"
#include "envoy/config/trace/v3/zipkin.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

ZipkinTracerFactory::ZipkinTracerFactory() : FactoryBase("envoy.tracers.zipkin") {}

Tracing::DriverSharedPtr ZipkinTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::ZipkinConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<Zipkin::Driver>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.serverFactoryContext().scope(), context.serverFactoryContext().threadLocal(),
      context.serverFactoryContext().runtime(), context.serverFactoryContext().localInfo(),
      context.serverFactoryContext().api().randomGenerator(),
      context.serverFactoryContext().timeSource());
}

/**
 * Static registration for the Zipkin tracer. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(ZipkinTracerFactory, Server::Configuration::TracerFactory, "envoy.zipkin");

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
