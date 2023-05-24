
#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/matcher/matcher.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

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

using xds::type::v3::CelExpression;

using CelMatcher = ::xds::type::matcher::v3::CelMatcher;
using CompiledExpressionPtr = std::unique_ptr<google::api::expr::runtime::CelExpression>;
using BaseActivationPtr = std::unique_ptr<google::api::expr::runtime::BaseActivation>;
using Builder = google::api::expr::runtime::CelExpressionBuilder;
using BuilderPtr = std::unique_ptr<Builder>;

// CEL matcher specific matching data
class CelMatchData : public ::Envoy::Matcher::CustomMatchData {
public:
  explicit CelMatchData(BaseActivationPtr activation) : activation_(std::move(activation)) {}
  BaseActivationPtr activation_;
};

class CelInputMatcher : public InputMatcher, public Logger::Loggable<Logger::Id::matcher> {
public:
  CelInputMatcher(const CelExpression& input_expr);

  bool match(const MatchingDataType& input) override;

  // TODO(tyxia) Formalize the validation approach. Use fixed string for now.
  absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{"cel_data_input"};
  }

private:
  // Expression builder must not be destroyed before the compiled expression.
  BuilderPtr expr_builder_;
  CompiledExpressionPtr compiled_expr_;
};

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
