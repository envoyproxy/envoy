#include "source/extensions/matching/input_matchers/cel_matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

using ::Envoy::Extensions::Matching::Http::CelInput::CelMatchData;
using ::xds::type::v3::CelExpression;

CelInputMatcher::CelInputMatcher(CelMatcherSharedPtr cel_matcher,
                                 Filters::Common::Expr::BuilderInstanceSharedPtr builder)
    : builder_(builder), cel_matcher_(std::move(cel_matcher)) {
  const CelExpression& input_expr = cel_matcher_->expr_match();

  // First try to get expression from the new CEL canonical format
  auto expr = Filters::Common::Expr::getExpr(input_expr);
  if (expr.has_value()) {
    compiled_expr_ = Filters::Common::Expr::createExpression(builder_->builder(), expr.value());
    return;
  }

  // Fallback to handling legacy formats for backward compatibility
  switch (input_expr.expr_specifier_case()) {
  case CelExpression::ExprSpecifierCase::kParsedExpr: {
    // For legacy parsed_expr, we need to convert to the new format
    const auto& legacy_parsed = input_expr.parsed_expr();

    // Convert legacy expr to cel::expr::Expr format
    std::string serialized_expr;
    if (!legacy_parsed.expr().SerializeToString(&serialized_expr)) {
      throw EnvoyException("Failed to serialize legacy expression");
    }

    converted_expr_ = cel::expr::Expr();
    if (!converted_expr_->ParseFromString(serialized_expr)) {
      throw EnvoyException("Failed to convert legacy expression to new format");
    }

    compiled_expr_ = Filters::Common::Expr::createExpression(builder_->builder(), *converted_expr_);
    return;
  }
  case CelExpression::ExprSpecifierCase::kCheckedExpr: {
    // For legacy checked_expr, we need to convert to the new format
    const auto& legacy_checked = input_expr.checked_expr();

    // Convert legacy expr to cel::expr::Expr format
    std::string serialized_expr;
    if (!legacy_checked.expr().SerializeToString(&serialized_expr)) {
      throw EnvoyException("Failed to serialize legacy expression");
    }

    converted_expr_ = cel::expr::Expr();
    if (!converted_expr_->ParseFromString(serialized_expr)) {
      throw EnvoyException("Failed to convert legacy expression to new format");
    }

    compiled_expr_ = Filters::Common::Expr::createExpression(builder_->builder(), *converted_expr_);
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
