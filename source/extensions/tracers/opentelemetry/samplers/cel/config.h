#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the CELSampler. @see SamplerFactory.
 */
class CELSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Create a CEL Sampler.
   *
   * @param config Protobuf config for the sampler.
   * @param context A reference to the TracerFactoryContext.
   * @return SamplerSharedPtr
   */
  SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::tracers::opentelemetry::samplers::v3::CELSamplerConfig>();
  }
  std::string name() const override { return "envoy.tracers.opentelemetry.samplers.cel"; }
};

DECLARE_FACTORY(AlwaysOnSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
