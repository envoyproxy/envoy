#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the dynamic opentracing tracer. @see HttpTracerFactory.
 */
class DynamicOpenTracingHttpTracerFactory : public HttpTracerFactory {
public:
  // HttpTracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const Json::Object& json_config, Server::Instance& server,
                                          Upstream::ClusterManager& cluster_manager) override;

  std::string name() override;

  bool requiresClusterName() const override { return false; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
