#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/tracer_config.h"

#include "envoy/tracing/trace_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {


/**
 * @brief The base type for all samplers
 *
 */
class Sampler {
public:
  virtual ~Sampler() = default;

  /**
   * @brief Decides if a trace should be sampled.
   *
   * @return true if trace should be sampled, false otherwise
   */
  virtual bool sample(Tracing::TraceContext& trace_context) = 0;
};

using SamplerPtr = std::shared_ptr<Sampler>;

/*
 * A factory for creating resource detectors that have configuration.
 */
class SamplerTypedFactory : public Envoy::Config::TypedFactory {
public:
  ~SamplerTypedFactory() override = default;

  /**
   * @brief Creates a sampler based on the configuration type provided.
   *
   * @param message The sampler configuration.
   * @param context The tracer factory context.
   * @return A sampler based on the configuration type provided.
   */
  virtual SamplerPtr
  createTypedSampler(const Protobuf::Message& message,
                              Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.samplers"; }
};

using SamplerTypedFactoryFactoryPtr = std::unique_ptr<SamplerTypedFactory>;

/*
 * A factory for creating sampler without configuration.
 */
class SamplerFactory : public Envoy::Config::UntypedFactory {
public:
  ~SamplerFactory() override = default;

  /**
   * @brief Creates a sampler that does not have a configuration.
   *
   * @param context The tracer factory context.
   * @return SamplerPtr
   */
  virtual SamplerPtr
  createSampler(Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.samplers"; }
};

using SamplerFactoryPtr = std::unique_ptr<SamplerFactory>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy