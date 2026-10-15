#pragma once

#include "envoy/server/tracer_config.h"

#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {

class DynamicModuleTracerFactory : public Server::Configuration::TracerFactory {
public:
  Tracing::DriverSharedPtr
  createTracerDriver(const Protobuf::Message& config,
                     Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.tracers.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
