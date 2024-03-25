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
  struct CelExpression {
    google::api::expr::v1alpha1::ParsedExpr parsed_expr_;
    Filters::Common::Expr::ExpressionPtr compiled_expr_;
  };

  ExpressionManager(Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder,
                    const LocalInfo::LocalInfo& local_info,
                    const Protobuf::RepeatedPtrField<std::string>& request_matchers,
                    const Protobuf::RepeatedPtrField<std::string>& response_matchers)
      : builder_(builder), local_info_(local_info),
        request_expr_(initExpressions(request_matchers)),
        response_expr_(initExpressions(response_matchers)){};

  bool hasRequestExpr() const { return !request_expr_.empty(); };

  bool hasResponseExpr() const { return !response_expr_.empty(); };

  ProtobufWkt::Struct
  evaluateRequestAttributes(const Filters::Common::Expr::Activation& activation) const {
    return evaluateAttributes(activation, request_expr_);
  }

  ProtobufWkt::Struct
  evaluateResponseAttributes(const Filters::Common::Expr::Activation& activation) const {
    return evaluateAttributes(activation, response_expr_);
  }

  static ProtobufWkt::Struct
  evaluateAttributes(const Filters::Common::Expr::Activation& activation,
                     const absl::flat_hash_map<std::string, CelExpression>& expr);

  const LocalInfo::LocalInfo& localInfo() const { return local_info_; };

private:
  absl::flat_hash_map<std::string, CelExpression>
  initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers);

  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;
  const LocalInfo::LocalInfo& local_info_;

  const absl::flat_hash_map<std::string, CelExpression> request_expr_;
  const absl::flat_hash_map<std::string, CelExpression> response_expr_;
};

std::vector<Matchers::StringMatcherPtr>
initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list,
                   Server::Configuration::CommonFactoryContext& context);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
