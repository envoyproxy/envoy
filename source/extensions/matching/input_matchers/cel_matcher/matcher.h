#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/matcher/matcher.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/matching/http/cel_input/cel_input.h"

#include "absl/types/variant.h"
#include "xds/type/matcher/v3/cel.pb.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

using ::Envoy::Matcher::InputMatcher;
using ::Envoy::Matcher::InputMatcherFactoryCb;
using ::Envoy::Matcher::MatchingDataType;

using CelMatcher = ::xds::type::matcher::v3::CelMatcher;
using BaseActivationPtr = std::unique_ptr<google::api::expr::runtime::BaseActivation>;
using CelMatcherSharedPtr = std::shared_ptr<::xds::type::matcher::v3::CelMatcher>;

class CelInputMatcher : public InputMatcher, public Logger::Loggable<Logger::Id::matcher> {
public:
  CelInputMatcher(CelMatcherSharedPtr cel_matcher,
                  Filters::Common::Expr::BuilderInstanceSharedConstPtr builder);

  bool match(const MatchingDataType& input) override;

  // TODO(tyxia) Formalize the validation approach. Use fixed string for now.
  absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{"cel_data_input"};
  }

private:
  const Filters::Common::Expr::CompiledExpression compiled_expr_;
};

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
