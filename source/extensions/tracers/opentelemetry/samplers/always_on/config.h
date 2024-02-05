#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/always_on_sampler.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the AlwaysOnSampler. @see SamplerFactory.
 */
class AlwaysOnSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Create a Sampler. @see AlwaysOnSampler
   *
   * @param config Protobuf config for the sampler.
   * @param context A reference to the TracerFactoryContext.
   * @return SamplerSharedPtr
   */
  SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::tracers::opentelemetry::samplers::v3::AlwaysOnSamplerConfig>();
  }
  std::string name() const override { return "envoy.tracers.opentelemetry.samplers.always_on"; }
};

DECLARE_FACTORY(AlwaysOnSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
