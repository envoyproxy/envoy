#include "source/common/matcher/cel_matcher.h"

namespace Envoy {
namespace Matcher {

CelInputMatcher::CelInputMatcher(const google::api::expr::v1alpha1::CheckedExpr& input_expr,
                                 Builder& builder) {
  auto cel_expression_status = builder.CreateExpression(&input_expr);
  // if (!cel_expression_status.ok()) {
  //   throw EnvoyException(
  //       absl::StrCat("failed to create an expression: ",
  //       cel_expression_status.status().message()));
  // }
  if (cel_expression_status.ok()) {
    compiled_expr_ = std::move(cel_expression_status.value());
  } else {
    ENVOY_LOG(debug, absl::StrCat("failed to create an expression: ",
                                  cel_expression_status.status().message()));
  }
}

bool CelInputMatcher::match(const Matcher::MatchingDataType& input) {
  Protobuf::Arena arena;
  if (auto* ptr = absl::get_if<std::shared_ptr<Matcher::CustomMatchData>>(&input); ptr != nullptr) {
    CelMatchData* cel_data = dynamic_cast<CelMatchData*>((*ptr).get());
    if (compiled_expr_ == nullptr || cel_data == nullptr) {
      return false;
    }

    auto eval_result = compiled_expr_->Evaluate(cel_data->data_, &arena);
    if (eval_result.ok() && eval_result.value().IsBool()) {
      return eval_result.value().BoolOrDie();
    }
  }

  return false;
}

// bool CelInputMatcher::match(const Matcher::MatchingDataType& input) {
//   Protobuf::Arena arena;
//   // TODO(tyxia) think about unique_ptr
//   // if (auto* ptr = absl::get_if<std::unique_ptr<Matcher::CustomMatchData>>(&input); ptr !=
//   // nullptr) {
//   if (auto* ptr = absl::get_if<std::shared_ptr<Matcher::CustomMatchData>>(&input); ptr !=
//   nullptr) {
//     CelMatchData* cel_data = dynamic_cast<CelMatchData*>((*ptr).get());
//     if (compiled_expr_ != nullptr && cel_data != nullptr) {
//       auto eval_result = compiled_expr_->Evaluate(cel_data->data_, &arena);
//       if (!eval_result.ok() || !eval_result.value().IsBool()) {
//         return false;
//       }
//       return eval_result.value().BoolOrDie();
//     }
//   }

//  return false;
//}

REGISTER_FACTORY(CelInputMatcherFactory, Matcher::InputMatcherFactory);

} // namespace Matcher
} // namespace Envoy
