#include "extensions/tracers/lightstep/config.h"

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
    const envoy::config::trace::v2::LightstepConfig& proto_config, Server::Instance& server) {
  auto opts = std::make_unique<lightstep::LightStepTracerOptions>();
  const auto access_token_file = server.api().fileReadToEnd(proto_config.access_token_file());
  const auto access_token_sv = StringUtil::rtrim(access_token_file);
  opts->access_token.assign(access_token_sv.data(), access_token_sv.size());
  opts->component_name = server.localInfo().clusterName();

  Tracing::DriverPtr lightstep_driver = std::make_unique<LightStepDriver>(
      proto_config, server.clusterManager(), server.stats(), server.threadLocal(), server.runtime(),
      std::move(opts), Common::Ot::OpenTracingDriver::PropagationMode::TracerNative);
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(lightstep_driver), server.localInfo());
}

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<LightstepTracerFactory, Server::Configuration::TracerFactory>
    register_;

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
