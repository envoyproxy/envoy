#include "server/config/http/dynamic_opentracing_http_tracer.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/config/well_known_names.h"
#include "common/tracing/dynamic_opentracing_driver_impl.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Tracing::HttpTracerPtr DynamicOpenTracingHttpTracerFactory::createHttpTracer(
    const Json::Object& json_config, Server::Instance& server,
    Upstream::ClusterManager& /*cluster_manager*/) {
  const std::string library = json_config.getString("library");
  const std::string config = json_config.getObject("config")->asJsonString();
  Tracing::DriverPtr dynamic_driver{
      std::make_unique<Tracing::DynamicOpenTracingDriver>(server.stats(), library, config)};
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(dynamic_driver), server.localInfo());
}

std::string DynamicOpenTracingHttpTracerFactory::name() {
  return Config::HttpTracerNames::get().DYNAMIC_OT;
}

/**
 * Static registration for the dynamic opentracing http tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<DynamicOpenTracingHttpTracerFactory, HttpTracerFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
