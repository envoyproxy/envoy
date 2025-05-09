#pragma once

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
/**
 * https://opentelemetry.io/docs/specs/otel/trace/sdk/#traceidratiobased
 * The TraceIdRatioBased MUST ignore the parent SampledFlag. To respect the parent SampledFlag, the
 * TraceIdRatioBased should be used as a delegate of the ParentBased sampler.
 */
class TraceIdRatioBasedSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  explicit TraceIdRatioBasedSampler(const envoy::extensions::tracers::opentelemetry::samplers::v3::
                                        TraceIdRatioBasedSamplerConfig& /*config*/,
                                    Server::Configuration::TracerFactoryContext& /*context*/);

  SamplingResult shouldSample(const StreamInfo::StreamInfo& stream_info,
                              const absl::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;
  std::string getDescription() const override;
  uint64_t traceIdToUint64(const std::string& trace_id) noexcept;

private:
  std::string description_;
  const envoy::type::v3::FractionalPercent sampling_percentage_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
