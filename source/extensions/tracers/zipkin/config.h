#pragma once

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Config registration for the zipkin tracer. @see TracerFactory.
 */
class ZipkinTracerFactory : public Server::Configuration::TracerFactory {
public:
  // TracerFactory
  Tracing::HttpTracerPtr createHttpTracer(const envoy::config::trace::v2::Tracing& configuration,
                                          Server::Instance& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::trace::v2::ZipkinConfig>();
  }

  std::string name() override;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
