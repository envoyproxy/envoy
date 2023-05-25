#include "source/extensions/matching/input_matchers/cel_matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

CelInputMatcher::CelInputMatcher(const CelExpression& input_expr) {
  expr_builder_ = Extensions::Filters::Common::Expr::createBuilder(nullptr);
  switch (input_expr.expr_specifier_case()) {
  case CelExpression::ExprSpecifierCase::kParsedExpr:
    compiled_expr_ =
        Filters::Common::Expr::createExpression(*expr_builder_, input_expr.parsed_expr().expr());
    return;
  case CelExpression::ExprSpecifierCase::kCheckedExpr:
    compiled_expr_ =
        Filters::Common::Expr::createExpression(*expr_builder_, input_expr.checked_expr().expr());
    return;
  case CelExpression::ExprSpecifierCase::EXPR_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool CelInputMatcher::match(const MatchingDataType& input) {
  Protobuf::Arena arena;
  if (auto* ptr = absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&input);
      ptr != nullptr) {
    CelMatchData* cel_data = dynamic_cast<CelMatchData*>((*ptr).get());
    // Compiled expression should not be nullptr at this point because the program should have
    // encountered a panic in the constructor earlier if any such error cases occurred. CEL matching
    // data should also not be nullptr since any errors should have been thrown by the CEL library
    // already.
    ASSERT(compiled_expr_ != nullptr && cel_data != nullptr);

    auto eval_result = compiled_expr_->Evaluate(*cel_data->activation_, &arena);
    if (eval_result.ok() && eval_result.value().IsBool()) {
      return eval_result.value().BoolOrDie();
    }
  }

  return false;
}

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
