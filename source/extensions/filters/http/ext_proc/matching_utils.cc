#include "source/extensions/filters/http/ext_proc/matching_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

absl::flat_hash_map<std::string, ExpressionManager::ExpressionPtrWithExpr>
ExpressionManager::initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) const {
  absl::flat_hash_map<std::string, ExpressionManager::ExpressionPtrWithExpr> expressions;
#if defined(USE_CEL_PARSER)
  for (const auto& matcher : matchers) {
    auto parse_status = google::api::expr::parser::Parse(matcher);
    if (!parse_status.ok()) {
      throw EnvoyException("Unable to parse descriptor expression: " +
                           parse_status.status().ToString());
    }
    const auto parse_status_expr = parse_status.value().expr();
    auto expression =
        Extensions::Filters::Common::Expr::createExpression(builder_->builder(), parse_status_expr);
    ExpressionPtrWithExpr expr(parse_status_expr, expression.get());
    std::cout << "expression_ptr_ after construction: ";
    std::cout << expr.expression_ptr_.get() << std::endl;
    expressions.try_emplace(matcher, std::move(expr));
    std::cout << "expression_ptr_ after placing in container: ";
    std::cout << expressions.at(matcher).expression_ptr_.get() << std::endl;
  }
#else
  ENVOY_LOG(warn, "CEL expression parsing is not available for use in this environment."
                  " Attempted to parse " +
                      std::to_string(matchers.size()) + " expressions");
#endif
  return expressions;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
