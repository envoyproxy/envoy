#pragma once

#include <string>

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/always_on_sampler.pb.h"

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
   * @brief Create a Sampler which samples every span
   *
   * @param context
   * @return SamplerPtr
   */
  SamplerPtr
  createSampler(const Protobuf::Message& config) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::tracers::opentelemetry::samplers::v3::
                                AlwaysOnSamplerConfig>();
  }
  std::string name() const override {
    return "envoy.tracers.opentelemetry.samplers.always_on";
  }
};

DECLARE_FACTORY(AlwaysOnSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy