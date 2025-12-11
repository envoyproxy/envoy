#include "source/extensions/access_loggers/filters/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

namespace Expr = Envoy::Extensions::Filters::Common::Expr;

CELAccessLogExtensionFilter::CELAccessLogExtensionFilter(
    const ::Envoy::LocalInfo::LocalInfo& local_info,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    const cel::expr::Expr& input_expr)
    : local_info_(local_info), expr_([&]() {
        auto compiled_expr =
            Extensions::Filters::Common::Expr::CompiledExpression::Create(builder, input_expr);
        if (!compiled_expr.ok()) {
          throw EnvoyException(
              absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
        }
        return std::move(compiled_expr.value());
      }()) {}

bool CELAccessLogExtensionFilter::evaluate(const Formatter::Context& log_context,
                                           const StreamInfo::StreamInfo& stream_info) const {
  Protobuf::Arena arena;
  const auto result =
      expr_.evaluate(arena, &local_info_, stream_info, log_context.requestHeaders().ptr(),
                     log_context.responseHeaders().ptr(), log_context.responseTrailers().ptr());
  if (!result.has_value() || result.value().IsError()) {
    return false;
  }
  auto eval_result = result.value();
  return eval_result.IsBool() ? eval_result.BoolOrDie() : false;
}

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
