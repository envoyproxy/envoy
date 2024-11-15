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
 * @brief A sampler which samples every span.
 * https://opentelemetry.io/docs/specs/otel/trace/sdk/#alwayson
 * - Returns RecordAndSample always.
 * - Description MUST be AlwaysOnSampler.
 *
 */
class AlwaysOnSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  explicit AlwaysOnSampler(const Protobuf::Message& /*config*/,
                           Server::Configuration::TracerFactoryContext& /*context*/) {}
  SamplingResult shouldSample(const absl::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;
  std::string getDescription() const override;

private:
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
