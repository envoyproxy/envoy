#pragma once

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

#include "absl/strings/string_view.h"

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
                              const std::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;
  std::string getDescription() const override;
  uint64_t traceIdToUint64(const std::string& trace_id) noexcept;

  /**
   * @return the randomness value R of the OpenTelemetry consistent probability sampling
   * specification: the 7 rightmost bytes (56 bits) of the trace id, which W3C Trace Context Level 2
   * requires to be random. Returns 0 if the trace id is not 32 hex characters.
   */
  static uint64_t traceIdRandomness(absl::string_view trace_id);

  /**
   * @return the rejection threshold T for the sampling percentage: a trace is sampled when R >= T.
   */
  static uint64_t rejectionThreshold(const envoy::type::v3::FractionalPercent& percentage);

private:
  std::string description_;
  const envoy::type::v3::FractionalPercent sampling_percentage_;
  const uint64_t rejection_threshold_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
