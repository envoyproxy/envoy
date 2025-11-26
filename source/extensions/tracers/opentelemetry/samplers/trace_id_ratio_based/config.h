#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the TraceIdRatioBasedSampler. @see SamplerFactory.
 */
class TraceIdRatioBasedSamplerFactory : public SamplerFactory {
public:
  /**
   * @brief Create a Sampler. @see TraceIdRatioBasedSampler
   *
   * @param config Protobuf config for the sampler.
   * @param context A reference to the TracerFactoryContext.
   * @return SamplerSharedPtr
   */
  SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.samplers.trace_id_ratio_based";
  }
};

DECLARE_FACTORY(TraceIdRatioBasedSamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
