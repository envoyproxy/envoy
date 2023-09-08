#pragma once

#include <string>

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the AllSampler. @see SamplerFactory.
 */
class AllSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Create a Sampler which samples every span
   *
   * @param context
   * @return SamplerPtr
   */
  SamplerPtr
  createSampler(Server::Configuration::TracerFactoryContext& context) override;

  std::string name() const override {
    return "envoy.tracers.opentelemetry.samplers.all";
  }
};

DECLARE_FACTORY(AllSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy