#include "source/extensions/tracers/lightstep/config.h"

#include "envoy/config/trace/v3/lightstep.pb.h"
#include "envoy/config/trace/v3/lightstep.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
#include "source/extensions/tracers/lightstep/lightstep_tracer_impl.h"

#include "lightstep/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

LightstepTracerFactory::LightstepTracerFactory() : FactoryBase("envoy.tracers.lightstep") {}

Tracing::DriverSharedPtr LightstepTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::LightstepConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  auto opts = std::make_unique<lightstep::LightStepTracerOptions>();
  if (proto_config.has_access_token()) {
    opts->access_token = Config::DataSource::read(proto_config.access_token(), true,
                                                  context.serverFactoryContext().api());
  } else {
    const auto access_token_file = context.serverFactoryContext().api().fileSystem().fileReadToEnd(
        proto_config.access_token_file());
    const auto access_token_sv = StringUtil::rtrim(access_token_file);
    opts->access_token.assign(access_token_sv.data(), access_token_sv.size());
  }
  opts->component_name = context.serverFactoryContext().localInfo().clusterName();

  return std::make_shared<LightStepDriver>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.serverFactoryContext().scope(), context.serverFactoryContext().threadLocal(),
      context.serverFactoryContext().runtime(), std::move(opts),
      Common::Ot::OpenTracingDriver::PropagationMode::TracerNative,
      context.serverFactoryContext().grpcContext());
}

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(LightstepTracerFactory, Server::Configuration::TracerFactory){"envoy.lightstep"};

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
