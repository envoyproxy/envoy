#pragma once

#include <string>

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.h"

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
   * @brief Create a Sampler which samples every span
   *
   * @param context
   * @return SamplerPtr
   */
  SamplerPtr
  createSampler(const Protobuf::Message& message) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::tracers::opentelemetry::samplers::v3::
                                DynatraceSamplerConfig>();
  }
  std::string name() const override {
    return "envoy.tracers.opentelemetry.samplers.dynatrace";
  }
};

DECLARE_FACTORY(DynatraceSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy