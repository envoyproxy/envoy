#pragma once

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/parent_based_sampler.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * This is a sampler decorator. If the parent context is empty or doesn't have a valid traceId,
 * the ParentBasedSampler will delegate the decision to the wrapped sampler.
 * Otherwise, it will decide based on the sampled flag of the parent context:
 * if parent_context->sampled -> return RecordAndSample
 * else -> return Decision::Drop
 * Check https://opentelemetry.io/docs/specs/otel/trace/sdk/#parentbased for the official docs and
 * https://github.com/open-telemetry/opentelemetry-cpp/blob/eb2b9753ea2df64079e07d40489388ea1b323108/sdk/src/trace/samplers/parent.cc#L30
 * for an official implementation
 */
class ParentBasedSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  explicit ParentBasedSampler(const Protobuf::Message& /*config*/,
                              Server::Configuration::TracerFactoryContext& /*context*/,
                              SamplerSharedPtr wrapped_sampler)
      : wrapped_sampler_(wrapped_sampler) {}
  SamplingResult shouldSample(const StreamInfo::StreamInfo& stream_info,
                              const absl::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;
  std::string getDescription() const override;

private:
  SamplerSharedPtr wrapped_sampler_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
