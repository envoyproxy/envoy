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
class DynatraceSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  explicit DynatraceSampler(const Protobuf::Message& /*config*/) : counter_(0) {}
  SamplingResult shouldSample(absl::StatusOr<SpanContext>& parent_context,
                              const std::string& trace_id, const std::string& name,
                              ::opentelemetry::proto::trace::v1::Span::SpanKind spankind,
                              const std::map<std::string, std::string>& attributes, const std::set<SpanContext> links) override;
  std::string getDescription() const override;

private:
  std::atomic<uint32_t> counter_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
