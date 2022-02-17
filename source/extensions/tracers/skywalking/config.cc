#include "source/extensions/tracers/skywalking/config.h"

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/config/trace/v3/skywalking.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/extensions/tracers/skywalking/skywalking_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

SkyWalkingTracerFactory::SkyWalkingTracerFactory() : FactoryBase("envoy.tracers.skywalking") {}

Tracing::DriverSharedPtr SkyWalkingTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<SkyWalking::Driver>(proto_config, context);
}

/**
 * Static registration for the SkyWalking tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(SkyWalkingTracerFactory, Server::Configuration::TracerFactory);

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
