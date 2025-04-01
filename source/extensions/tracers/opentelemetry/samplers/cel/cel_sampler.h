#pragma once

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace Expr = Envoy::Extensions::Filters::Common::Expr;

/**
 * @brief A sampler which samples on CEL expression.
 *
 * - Returns RecordAndSample always.
 * - Description MUST be AlwaysOnSampler.
 *
 */
class CELSampler : public Sampler, Logger::Loggable<Logger::Id::tracing> {
public:
  CELSampler(const ::Envoy::LocalInfo::LocalInfo& local_info,
             Expr::BuilderInstanceSharedPtr builder, const google::api::expr::v1alpha1::Expr& expr);
  SamplingResult shouldSample(const StreamInfo::StreamInfo& stream_info,
                              const absl::optional<SpanContext> parent_context,
                              const std::string& trace_id, const std::string& name,
                              OTelSpanKind spankind,
                              OptRef<const Tracing::TraceContext> trace_context,
                              const std::vector<SpanContext>& links) override;
  std::string getDescription() const override;

private:
  const ::Envoy::LocalInfo::LocalInfo& local_info_;
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;
  const google::api::expr::v1alpha1::Expr parsed_expr_;
  Extensions::Filters::Common::Expr::ExpressionPtr compiled_expr_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
