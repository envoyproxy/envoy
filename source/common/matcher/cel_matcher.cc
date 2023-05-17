#include "source/common/matcher/cel_matcher.h"

namespace Envoy {
namespace Matcher {

bool CelInputMatcher::match(const Matcher::MatchingDataType& input) {
  return false;
  Protobuf::Arena arena;
  // TODO(tyxia) think about unique_ptr
  // if (auto* ptr = absl::get_if<std::unique_ptr<Matcher::CustomMatchData>>(&input); ptr !=
  // nullptr) {
  if (auto* ptr = absl::get_if<std::shared_ptr<Matcher::CustomMatchData>>(&input); ptr != nullptr) {
    CelMatchData* cel_data = dynamic_cast<CelMatchData*>((*ptr).get());
    if (cel_data != nullptr) {
      std::cout << "tyxia_called_haha" << std::endl;

      // auto eval_result = compiled_expr_->Evaluate(cel_data->data_, &arena);
      // if (!eval_result.ok() || !eval_result.value().IsBool()) {
      //   return false;
      // }
      // return eval_result.value().BoolOrDie();
    }
    return false;
  }

  return false;
}

REGISTER_FACTORY(CelInputMatcherFactory, Matcher::InputMatcherFactory);

} // namespace Matcher
} // namespace Envoy