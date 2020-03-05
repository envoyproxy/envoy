#include "extensions/tracers/lightstep/config.h"

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/lightstep/lightstep_tracer_impl.h"
#include "extensions/tracers/well_known_names.h"

#include "lightstep/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

LightstepTracerFactory::LightstepTracerFactory() : FactoryBase(TracerNames::get().Lightstep) {}

Tracing::HttpTracerPtr LightstepTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v3::LightstepConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  auto opts = std::make_unique<lightstep::LightStepTracerOptions>();
  const auto access_token_file = context.serverFactoryContext().api().fileSystem().fileReadToEnd(
      proto_config.access_token_file());
  const auto access_token_sv = StringUtil::rtrim(access_token_file);
  opts->access_token.assign(access_token_sv.data(), access_token_sv.size());
  opts->component_name = context.serverFactoryContext().localInfo().clusterName();

  Tracing::DriverPtr lightstep_driver = std::make_unique<LightStepDriver>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.serverFactoryContext().scope(), context.serverFactoryContext().threadLocal(),
      context.serverFactoryContext().runtime(), std::move(opts),
      Common::Ot::OpenTracingDriver::PropagationMode::TracerNative,
      context.serverFactoryContext().grpcContext());
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(lightstep_driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(LightstepTracerFactory, Server::Configuration::TracerFactory){"envoy.lightstep"};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
