#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include <memory>
#include <typeinfo>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

absl::flat_hash_map<std::string, std::unique_ptr<ExpressionManager::ExpressionPtrWithExpr>>
ExpressionManager::initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) const {
  absl::flat_hash_map<std::string, std::unique_ptr<ExpressionManager::ExpressionPtrWithExpr>>
      expressions;
#if defined(USE_CEL_PARSER)
  for (const auto& matcher : matchers) {
    auto parse_status = google::api::expr::parser::Parse(matcher);
    if (!parse_status.ok()) {
      throw EnvoyException("Unable to parse descriptor expression: " +
                           parse_status.status().ToString());
    }
    const google::api::expr::v1alpha1::Expr& parse_status_expr = parse_status.value().expr();
    const Filters::Common::Expr::ExpressionPtr& expression =
        Extensions::Filters::Common::Expr::createExpression(builder_->builder(), parse_status_expr);
    std::unique_ptr<ExpressionPtrWithExpr> expr =
        std::make_unique<ExpressionPtrWithExpr>(parse_status_expr, std::move(expression.get()));
    if (matcher == "request.path") {
      ExpressionManager::printExprPtrAndType(*expr, "after construction");
    }
    expressions.try_emplace(matcher, std::move(expr));
    if (matcher == "request.path") {
      ExpressionManager::printExprPtrAndType(*expressions.at(matcher),
                                             "after placing in container");
    }
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
