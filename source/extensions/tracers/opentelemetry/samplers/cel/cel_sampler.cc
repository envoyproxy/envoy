#include "source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

CELSampler::CELSampler(const ::Envoy::LocalInfo::LocalInfo& local_info,
                       Expr::BuilderInstanceSharedPtr builder,
                       const google::api::expr::v1alpha1::Expr& expr)
    : local_info_(local_info), builder_(builder), parsed_expr_(expr) {
  compiled_expr_ = Expr::createExpression(builder_->builder(), parsed_expr_);
}

SamplingResult CELSampler::shouldSample(const StreamInfo::StreamInfo& stream_info,
                                        const absl::optional<SpanContext> parent_context,
                                        const std::string& /*trace_id*/,
                                        const std::string& /*name*/, OTelSpanKind /*kind*/,
                                        OptRef<const Tracing::TraceContext> trace_context,
                                        const std::vector<SpanContext>& /*links*/) {

  Protobuf::Arena arena;
  const ::Envoy::Http::RequestHeaderMap* request_headers = nullptr;
  if (trace_context.has_value() && trace_context->requestHeaders().has_value()) {
    request_headers = trace_context->requestHeaders().ptr();
  }

  auto eval_status = Expr::evaluate(
      *compiled_expr_, arena, &local_info_, stream_info, request_headers /* request_headers */,
      nullptr /* response_headers */, nullptr /* response_trailers */);
  SamplingResult result;
  if (!eval_status.has_value() || eval_status.value().IsError()) {
    result.decision = Decision::Drop;
    return result;
  }
  auto eval_result_val = eval_status.value();
  auto eval_result = eval_result_val.IsBool() ? eval_result_val.BoolOrDie() : false;
  if (!eval_result) {
    result.decision = Decision::Drop;
    return result;
  }

  result.decision = Decision::RecordAndSample;
  if (parent_context.has_value()) {
    result.tracestate = parent_context.value().tracestate();
  }
  return result;
}

std::string CELSampler::getDescription() const { return "CELSampler"; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
