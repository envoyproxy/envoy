#pragma once

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExpressionManager : public Logger::Loggable<Logger::Id::ext_proc> {
public:
  ExpressionManager(Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder,
                    const Protobuf::RepeatedPtrField<std::string>& request_matchers,
                    const Protobuf::RepeatedPtrField<std::string>& response_matchers)
      : builder_(builder), request_expr_(initExpressions(request_matchers)),
        response_expr_(initExpressions(response_matchers)){};

  bool hasRequestExpr() const { return !request_expr_.empty(); };

  bool hasResponseExpr() const { return !response_expr_.empty(); };

  std::unique_ptr<ProtobufWkt::Struct>
  evaluateRequestAttributes(const Filters::Common::Expr::Activation& activation) const {
    return evaluateAttributes(activation, request_expr_);
  }

  std::unique_ptr<ProtobufWkt::Struct>
  evaluateResponseAttributes(const Filters::Common::Expr::Activation& activation) const {
    return evaluateAttributes(activation, response_expr_);
  }

  static std::unique_ptr<ProtobufWkt::Struct> evaluateAttributes(
      const Filters::Common::Expr::Activation& activation,
      const absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr>& expr);

private:
  // This list is required to maintain the lifetimes of expr objects on which compiled expressions
  // depend
  std::list<google::api::expr::v1alpha1::ParsedExpr> expr_list_;
  absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr>
  initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers);

  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;

  const absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr> request_expr_;
  const absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr> response_expr_;
};

const std::vector<Matchers::StringMatcherPtr>
initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
