#include "source/extensions/access_loggers/filters/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

namespace Expr = Envoy::Extensions::Filters::Common::Expr;

CELAccessLogExtensionFilter::CELAccessLogExtensionFilter(
    const ::Envoy::LocalInfo::LocalInfo& local_info,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder,
    const cel::expr::Expr& input_expr)
    : local_info_(local_info), builder_(builder), parsed_expr_(input_expr) {
  compiled_expr_ =
      Extensions::Filters::Common::Expr::createExpression(builder_->builder(), parsed_expr_);
}

bool CELAccessLogExtensionFilter::evaluate(const Formatter::HttpFormatterContext& log_context,
                                           const StreamInfo::StreamInfo& stream_info) const {
  ProtobufWkt::Arena arena;
  const auto result = Extensions::Filters::Common::Expr::evaluate(
      *compiled_expr_.get(), arena, &local_info_, stream_info, &log_context.requestHeaders(),
      &log_context.responseHeaders(), &log_context.responseTrailers());
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
