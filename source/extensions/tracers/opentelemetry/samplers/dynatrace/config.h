#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the DynatraceSampler. @see SamplerFactory.
 */
class DynatraceSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Creates a Dynatrace sampler
   *
   * @param config The sampler configuration
   * @param context The tracer factory context.
   * @return SamplerSharedPtr
   */
  SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig>();
  }
  std::string name() const override { return "envoy.tracers.opentelemetry.samplers.dynatrace"; }
};

DECLARE_FACTORY(DynatraceSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
