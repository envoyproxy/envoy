#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/parent_based_sampler.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the ParentBasedSampler. @see SamplerFactory.
 */
class ParentBasedSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Create a Sampler. @see ParentBasedSampler
   *
   * @param config Protobuf config for the sampler.
   * @param context A reference to the TracerFactoryContext.
   * @return SamplerSharedPtr
   */
  SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::tracers::opentelemetry::samplers::v3::ParentBasedSamplerConfig>();
  }
  std::string name() const override { return "envoy.tracers.opentelemetry.samplers.parent_based"; }
};

DECLARE_FACTORY(ParentBasedSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
