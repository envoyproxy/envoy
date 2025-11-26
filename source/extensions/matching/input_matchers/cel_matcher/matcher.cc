#include "source/extensions/matching/input_matchers/cel_matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

using ::Envoy::Extensions::Matching::Http::CelInput::CelMatchData;
using ::xds::type::v3::CelExpression;

CelInputMatcher::CelInputMatcher(CelMatcherSharedPtr cel_matcher,
                                 Filters::Common::Expr::BuilderInstanceSharedConstPtr builder)
    : compiled_expr_([&]() {
        auto compiled_expr =
            Filters::Common::Expr::CompiledExpression::Create(builder, cel_matcher->expr_match());
        if (!compiled_expr.ok()) {
          throw EnvoyException(
              absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
        }
        return std::move(compiled_expr.value());
      }()) {}

bool CelInputMatcher::match(const MatchingDataType& input) {
  Protobuf::Arena arena;
  if (auto* ptr = absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&input);
      ptr != nullptr) {
    CelMatchData* cel_data = dynamic_cast<CelMatchData*>((*ptr).get());
    // CEL matching data should also not be nullptr since any errors should
    // have been thrown by the CEL library already.
    ASSERT(cel_data != nullptr);

    auto eval_result = compiled_expr_.evaluate(*cel_data->activation_, &arena);
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
