#include "extensions/tracers/dynamic_ot/config.h"

#include "envoy/config/trace/v3/dynamic_ot.pb.h"
#include "envoy/config/trace/v3/dynamic_ot.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/dynamic_ot/dynamic_opentracing_driver_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

DynamicOpenTracingTracerFactory::DynamicOpenTracingTracerFactory()
    : FactoryBase("envoy.tracers.dynamic_ot") {}

Tracing::HttpTracerSharedPtr DynamicOpenTracingTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v3::DynamicOtConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  const std::string& library = proto_config.library();
  const ProtobufWkt::Struct& config_struct = proto_config.config();
  const std::string config = MessageUtil::getJsonStringFromMessageOrDie(config_struct);
  Tracing::DriverPtr dynamic_driver = std::make_unique<DynamicOpenTracingDriver>(
      context.serverFactoryContext().scope(), library, config);
  return std::make_shared<Tracing::HttpTracerImpl>(std::move(dynamic_driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the dynamic opentracing tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamicOpenTracingTracerFactory,
                 Server::Configuration::TracerFactory){"envoy.dynamic.ot"};

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
