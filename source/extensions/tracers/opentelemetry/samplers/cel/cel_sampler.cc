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
                       Expr::BuilderInstanceSharedConstPtr builder,
                       const xds::type::v3::CelExpression& expr)
    : local_info_(local_info), compiled_expr_([&]() {
        auto compiled_expr = Expr::CompiledExpression::Create(builder, expr);
        if (!compiled_expr.ok()) {
          throw EnvoyException(
              absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
        }
        return std::move(compiled_expr.value());
      }()) {}

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

  auto eval_status = compiled_expr_.evaluate(
      arena, &local_info_, stream_info, request_headers /* request_headers */,
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

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
