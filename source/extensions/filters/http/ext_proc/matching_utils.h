#pragma once

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

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

  struct ExpressionPtrWithExpr {
    ExpressionPtrWithExpr(const google::api::expr::v1alpha1::Expr& expr,
                          const Filters::Common::Expr::ExpressionPtr& expr_ptr)
        : expression_ptr_(std::move(expr_ptr)), expr_(std::move(expr)){};
    const Filters::Common::Expr::ExpressionPtr& expression_ptr_;
    const google::api::expr::v1alpha1::Expr& expr_;
  };

  bool hasRequestExpr() const { return !request_expr_.empty(); };

  bool hasResponseExpr() const { return !response_expr_.empty(); };

  const absl::optional<ProtobufWkt::Struct>
  evaluateRequestAttributes(const Filters::Common::Expr::ActivationPtr& activation) const {
    return evaluateAttributes(activation, request_expr_);
  }

  const absl::optional<ProtobufWkt::Struct>
  evaluateResponseAttributes(const Filters::Common::Expr::ActivationPtr& activation) const {
    return evaluateAttributes(activation, response_expr_);
  }

  const absl::optional<ProtobufWkt::Struct>
  evaluateAttributes(const Filters::Common::Expr::ActivationPtr& activation,
                     const absl::flat_hash_map<std::string, ExpressionPtrWithExpr>& expr) const {
    std::cout << "entered evaluateAttributes" << std::endl;
    absl::optional<ProtobufWkt::Struct> proto;
    if (expr.size() > 0) {
      proto.emplace(ProtobufWkt::Struct{});
      for (const auto& hash_entry : expr) {
        std::cout << "evaluating attr" << std::endl;
        std::cout << "evaluating " << hash_entry.first << std::endl;
        ProtobufWkt::Arena arena;
        std::cout << "expression_ptr_: ";
        std::cout << hash_entry.second.expression_ptr_.get() << std::endl;
        std::cout << "activation: ";
        std::cout << activation.get() << std::endl;
        const auto result = hash_entry.second.expression_ptr_.get()->Evaluate(*activation, &arena);
        if (!result.ok()) {
          std::cout << "!result.ok()" << std::endl;
          // TODO: Stats?
          continue;
        }
        std::cout << "result.ok()" << std::endl;

        if (result.value().IsError()) {
          ENVOY_LOG(trace, "error parsing cel expression {}", hash_entry.first);
          continue;
        }

        ProtobufWkt::Value value;
        switch (result.value().type()) {
        case google::api::expr::runtime::CelValue::Type::kBool:
          value.set_bool_value(result.value().BoolOrDie());
          break;
        case google::api::expr::runtime::CelValue::Type::kNullType:
          value.set_null_value(ProtobufWkt::NullValue{});
          break;
        case google::api::expr::runtime::CelValue::Type::kDouble:
          value.set_number_value(result.value().DoubleOrDie());
          break;
        default:
          value.set_string_value(Filters::Common::Expr::print(result.value()));
        }

        (*(proto.value()).mutable_fields())[hash_entry.first] = value;
      }
    }

    return proto;
  }

private:
  absl::flat_hash_map<std::string, ExpressionPtrWithExpr>
  initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) const {
    absl::flat_hash_map<std::string, ExpressionPtrWithExpr> expressions;
#if defined(USE_CEL_PARSER)
    for (const auto& matcher : matchers) {
      auto parse_status = google::api::expr::parser::Parse(matcher);
      if (!parse_status.ok()) {
        throw EnvoyException("Unable to parse descriptor expression: " +
                             parse_status.status().ToString());
      }
      auto parse_status_expr = parse_status.value().expr();
      auto expression = Extensions::Filters::Common::Expr::createExpression(builder_->builder(),
                                                                            parse_status_expr);
      const ExpressionPtrWithExpr expr(parse_status_expr, expression);
      std::cout << "expression_ptr_: ";
      std::cout << expr.expression_ptr_.get() << std::endl;
      expressions.try_emplace(matcher, std::move(expr));
    }
#else
    ENVOY_LOG(warn, "CEL expression parsing is not available for use in this environment."
                    " Attempted to parse " +
                        std::to_string(matchers.size()) + " expressions");
#endif
    return expressions;
  }
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;

  const absl::flat_hash_map<std::string, ExpressionPtrWithExpr> request_expr_;
  const absl::flat_hash_map<std::string, ExpressionPtrWithExpr> response_expr_;
};

class MatchingUtils : public Logger::Loggable<Logger::Id::ext_proc> {
public:
  static const std::vector<Matchers::StringMatcherPtr>
  initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list) {
    std::vector<Matchers::StringMatcherPtr> header_matchers;
    for (const auto& matcher : header_list.patterns()) {
      header_matchers.push_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              matcher));
    }
    return header_matchers;
  }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
