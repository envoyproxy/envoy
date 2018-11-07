#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Config registration for the Datadog tracer. @see TracerFactory.
 */
class DatadogTracerFactory : public Server::Configuration::TracerFactory {
public:
  // TracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const envoy::config::trace::v2::Tracing& configuration,
                                          Server::Instance& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::trace::v2::DatadogConfig>();
  }

  std::string name() override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
