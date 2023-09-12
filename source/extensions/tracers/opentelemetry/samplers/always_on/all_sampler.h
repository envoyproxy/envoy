#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A sampler which samples every span
 * variable.
 *
 */
class AlwaysOnSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  AlwaysOnSampler(Server::Configuration::TracerFactoryContext& context)
      : context_(context) {}
  SamplingResult shouldSample() override;
  std::string getDescription() const override;

private:
  Server::Configuration::TracerFactoryContext& context_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
