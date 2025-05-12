#include "source/extensions/access_loggers/filters/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

namespace Expr = Envoy::Extensions::Filters::Common::Expr;

CELAccessLogExtensionFilter::CELAccessLogExtensionFilter(
    const ::Envoy::LocalInfo::LocalInfo& local_info, Expr::BuilderInstanceSharedPtr builder,
    const google::api::expr::v1alpha1::Expr& input_expr)
    : local_info_(local_info), builder_(builder), parsed_expr_(input_expr) {
  compiled_expr_ = Expr::createExpression(builder_->builder(), parsed_expr_);
}

bool CELAccessLogExtensionFilter::evaluate(const Formatter::HttpFormatterContext& log_context,
                                           const StreamInfo::StreamInfo& stream_info) const {
  Protobuf::Arena arena;
  auto eval_status = Expr::evaluate(*compiled_expr_, arena, &local_info_, stream_info,
                                    &log_context.requestHeaders(), &log_context.responseHeaders(),
                                    &log_context.responseTrailers());
  if (!eval_status.has_value() || eval_status.value().IsError()) {
    return false;
  }
  auto result = eval_status.value();
  return result.IsBool() ? result.BoolOrDie() : false;
}

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
