#include "server/config/http/lightstep_http_tracer.h"

#include <string>

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/lightstep_tracer_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Tracing::HttpTracerPtr LightstepHttpTracerFactory::tryCreateHttpTracer(
    const std::string& type, const Json::Object& json_config, Server::Instance& server,
    Upstream::ClusterManager& cluster_manager) {
  if (type != "lightstep") {
    return nullptr;
  }

  Envoy::Runtime::RandomGenerator& rand = server.random();

  std::unique_ptr<lightstep::TracerOptions> opts(new lightstep::TracerOptions());
  opts->access_token = server.api().fileReadToEnd(json_config.getString("access_token_file"));
  StringUtil::rtrim(opts->access_token);

  opts->tracer_attributes["lightstep.component_name"] = server.localInfo().clusterName();
  opts->guid_generator = [&rand]() { return rand.random(); };

  Tracing::DriverPtr lightstep_driver(
      new Tracing::LightStepDriver(json_config, cluster_manager, server.stats(),
                                   server.threadLocal(), server.runtime(), std::move(opts)));
  return Tracing::HttpTracerPtr(
      new Tracing::HttpTracerImpl(std::move(lightstep_driver), server.localInfo()));
}

/**
 * Static registration for the lightstep http tracer. @see RegisterHttpTracerFactory.
 */
static RegisterHttpTracerFactory<LightstepHttpTracerFactory> register_;

} // Configuration
} // Server
} // Envoy
