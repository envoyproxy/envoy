#include "extensions/filters/common/expr/evaluator.h"

#include "envoy/common/exception.h"

#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

BuilderPtr createBuilder(Protobuf::Arena* arena) {
  google::api::expr::runtime::InterpreterOptions options;

  // Security-oriented defaults
  options.enable_comprehension = false;
  options.enable_regex = true;
  options.regex_max_program_size = 100;
  options.enable_string_conversion = false;
  options.enable_string_concat = false;
  options.enable_list_concat = false;

  // Enable constant folding (performance optimization)
  if (arena != nullptr) {
    options.constant_folding = true;
    options.constant_arena = arena;
  }

  auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
  auto register_status =
      google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry(), options);
  if (!register_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register built-in functions: ", register_status.message()));
  }
  return builder;
}

ExpressionPtr createExpression(Builder& builder, const google::api::expr::v1alpha1::Expr& expr) {
  google::api::expr::v1alpha1::SourceInfo source_info;
  auto cel_expression_status = builder.CreateExpression(&expr, &source_info);
  if (!cel_expression_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to create an expression: ", cel_expression_status.status().message()));
  }
  return std::move(cel_expression_status.ValueOrDie());
}

absl::optional<CelValue> evaluate(const Expression& expr, Protobuf::Arena* arena,
                                  const StreamInfo::StreamInfo& info,
                                  const Http::HeaderMap* request_headers,
                                  const Http::HeaderMap* response_headers,
                                  const Http::HeaderMap* response_trailers) {
  google::api::expr::runtime::Activation activation;
  const RequestWrapper request(request_headers, info);
  const ResponseWrapper response(response_headers, response_trailers, info);
  const ConnectionWrapper connection(info);
  const UpstreamWrapper upstream(info);
  const PeerWrapper source(info, false);
  const PeerWrapper destination(info, true);
  activation.InsertValue(Request, CelValue::CreateMap(&request));
  activation.InsertValue(Response, CelValue::CreateMap(&response));
  activation.InsertValue(Metadata, CelValue::CreateMessage(&info.dynamicMetadata(), arena));
  activation.InsertValue(Connection, CelValue::CreateMap(&connection));
  activation.InsertValue(Upstream, CelValue::CreateMap(&upstream));
  activation.InsertValue(Source, CelValue::CreateMap(&source));
  activation.InsertValue(Destination, CelValue::CreateMap(&destination));

  auto eval_status = expr.Evaluate(activation, arena);
  if (!eval_status.ok()) {
    return {};
  }

  return eval_status.ValueOrDie();
}

bool matches(const Expression& expr, const StreamInfo::StreamInfo& info,
             const Http::HeaderMap& headers) {
  Protobuf::Arena arena;
  auto eval_status = Expr::evaluate(expr, &arena, info, &headers, nullptr, nullptr);
  if (!eval_status.has_value()) {
    return false;
  }
  auto result = eval_status.value();
  return result.IsBool() ? result.BoolOrDie() : false;
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
