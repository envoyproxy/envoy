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

Tracing::HttpTracerPtr LightstepTracerFactory::createHttpTracer(const Json::Object& json_config,
                                                                Server::Instance& server) {

  std::unique_ptr<lightstep::LightStepTracerOptions> opts(new lightstep::LightStepTracerOptions());
  const auto access_token_file =
      server.api().fileReadToEnd(json_config.getString("access_token_file"));
  const auto access_token_sv = StringUtil::rtrim(access_token_file);
  opts->access_token.assign(access_token_sv.data(), access_token_sv.size());
  opts->component_name = server.localInfo().clusterName();

  Tracing::DriverPtr lightstep_driver{new LightStepDriver{
      json_config, server.clusterManager(), server.stats(), server.threadLocal(), server.runtime(),
      std::move(opts), Common::Ot::OpenTracingDriver::PropagationMode::TracerNative}};
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(lightstep_driver), server.localInfo());
}

std::string LightstepTracerFactory::name() { return TracerNames::get().LIGHTSTEP; }

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<LightstepTracerFactory, Server::Configuration::TracerFactory>
    register_;

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
