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

Tracing::HttpTracerPtr
LightstepTracerFactory::createHttpTracer(const envoy::config::trace::v2::Tracing& configuration,
                                         Server::Instance& server) {
  ProtobufTypes::MessagePtr config_ptr = createEmptyConfigProto();

  if (configuration.http().has_config()) {
    MessageUtil::jsonConvert(configuration.http().config(), *config_ptr);
  }

  const auto& lightstep_config =
      dynamic_cast<const envoy::config::trace::v2::LightstepConfig&>(*config_ptr);

  std::unique_ptr<lightstep::LightStepTracerOptions> opts(new lightstep::LightStepTracerOptions());
  const auto access_token_file = server.api().fileReadToEnd(lightstep_config.access_token_file());
  const auto access_token_sv = StringUtil::rtrim(access_token_file);
  opts->access_token.assign(access_token_sv.data(), access_token_sv.size());
  opts->component_name = server.localInfo().clusterName();

  Tracing::DriverPtr lightstep_driver{std::make_unique<LightStepDriver>(
      lightstep_config, server.clusterManager(), server.stats(), server.threadLocal(),
      server.runtime(), std::move(opts),
      Common::Ot::OpenTracingDriver::PropagationMode::TracerNative)};
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(lightstep_driver), server.localInfo());
}

std::string LightstepTracerFactory::name() { return TracerNames::get().Lightstep; }

/**
 * Static registration for the lightstep tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<LightstepTracerFactory, Server::Configuration::TracerFactory>
    register_;

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
