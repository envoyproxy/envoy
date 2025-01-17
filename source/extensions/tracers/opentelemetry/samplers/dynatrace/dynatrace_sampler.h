#pragma once

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampling_controller.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/stream_summary.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A Dynatrace specific sampler.
 *
 * For sampling, the requests are categorized based on the http method and the http path.
 * A sampling multiplicity is calculated for every request category based on the
 * number of requests. A Dynatrace specific tag is added to the http tracestate header.
 */
class DynatraceSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  DynatraceSampler(
      const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig& config,
      Server::Configuration::TracerFactoryContext& context,
      SamplerConfigProviderPtr sampler_config_provider);

  /** @see Sampler#shouldSample */
  SamplingResult shouldSample(const absl::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;

  std::string getDescription() const override;

private:
  std::string dt_tracestate_key_; // used as key in the http tracestate header
  Event::TimerPtr timer_;         // used to periodically calculate sampling multiplicity
  SamplingController sampling_controller_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
