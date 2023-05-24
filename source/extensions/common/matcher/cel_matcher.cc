#include "source/extensions/common/matcher/cel_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

CelInputMatcher::CelInputMatcher(const CelExpression& input_expr) {
  expr_builder_ = Extensions::Filters::Common::Expr::createBuilder(nullptr);
  switch (input_expr.expr_specifier_case()) {
  case CelExpression::ExprSpecifierCase::kParsedExpr: {
    compiled_expr_ =
        Filters::Common::Expr::createExpression(*expr_builder_, input_expr.parsed_expr().expr());
    return;
  }
  case CelExpression::ExprSpecifierCase::kCheckedExpr: {
    compiled_expr_ =
        Filters::Common::Expr::createExpression(*expr_builder_, input_expr.checked_expr().expr());
    return;
  }
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
    // Return false if we don't have compiled CEL expression or CEL input data is empty.
    if (compiled_expr_ == nullptr || cel_data == nullptr) {
      // TODO(tyxia) Add some logs
      return false;
    }

    auto eval_result = compiled_expr_->Evaluate(cel_data->data_, &arena);
    if (eval_result.ok() && eval_result.value().IsBool()) {
      return eval_result.value().BoolOrDie();
    }
  }

  return false;
}

REGISTER_FACTORY(CelInputMatcherFactory, Matcher::InputMatcherFactory);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
