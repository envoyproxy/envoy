#include "extensions/filters/common/expr/evaluator.h"

#include "envoy/common/exception.h"

#include "common/protobuf/protobuf.h"

#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

ExpressionPtr create(const google::api::expr::v1alpha1::Expr& expr) {
  google::api::expr::runtime::InterpreterOptions options;

  // Conformance with spec/go runtimes requires this setting
  options.partial_string_match = true;

  auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
  auto register_status =
      google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry());
  if (!register_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register built-in functions: ", register_status.message()));
  }
  google::api::expr::v1alpha1::SourceInfo source_info;
  auto cel_expression_status = builder->CreateExpression(&expr, &source_info);
  if (!cel_expression_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to create an expression: ", cel_expression_status.status().message()));
  }
  return std::move(cel_expression_status.ValueOrDie());
}

absl::optional<CelValue> evaluate(const Expression& expr, const StreamInfo::StreamInfo& info,
                                  const Http::HeaderMap& headers) {
  Protobuf::Arena arena;
  google::api::expr::runtime::Activation activation;
  const RequestWrapper request(headers, info);
  const ConnectionWrapper connection(info);
  activation.InsertValue(Request, CelValue::CreateMap(&request));
  activation.InsertValue(Metadata, CelValue::CreateMessage(&info.dynamicMetadata(), &arena));
  activation.InsertValue(Connection, CelValue::CreateMap(&connection));

  auto eval_status = expr.Evaluate(activation, &arena);
  if (!eval_status.ok()) {
    return {};
  }

  return eval_status.ValueOrDie();
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
