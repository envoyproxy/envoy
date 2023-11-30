#pragma once

#include <typeinfo>

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
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
        response_expr_(initExpressions(response_matchers)) {
    printExprPtrAndType(*request_expr_.at("request.path"), "in ExpressionManager constructor");
  };

  // This struct exists because the lifetime of the api expr must exceed expressions
  // parsed from it.
  struct ExpressionPtrWithExpr {
    ExpressionPtrWithExpr(const google::api::expr::v1alpha1::Expr& expr,
                          Filters::Common::Expr::Expression* expr_ptr)
        : expr_(expr), expression_ptr_(std::move(expr_ptr)){};
    ~ExpressionPtrWithExpr() {
      std::cout << "!!!!!! ExpressionPtrWithExpr is being destructed !!!!!!" << std::endl;
    };
    const google::api::expr::v1alpha1::Expr& expr_;
    Filters::Common::Expr::ExpressionPtr expression_ptr_;
  };

  static void printExprPtrAndType(const ExpressionPtrWithExpr& expr, std::string location) {
    std::cout << "expression_ptr_ address " << location << ": " << expr.expression_ptr_.get()
              << std::endl;
    auto& val = *expr.expression_ptr_.get();
    std::cout << "expression_ptr_ type " << location << ": " << typeid(val).name() << std::endl;
    std::cout << "expr_ address " << location << ": " << &expr.expr_ << std::endl;
    std::cout << "expr_ type " << location << ": " << typeid(expr.expr_).name() << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
  }

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

  static const absl::optional<ProtobufWkt::Struct> evaluateAttributes(
      const Filters::Common::Expr::ActivationPtr& activation,
      const absl::flat_hash_map<std::string, std::unique_ptr<ExpressionPtrWithExpr>>& expr) {
    absl::optional<ProtobufWkt::Struct> proto;
    if (!expr.empty()) {
      proto.emplace(ProtobufWkt::Struct{});
      for (const auto& hash_entry : expr) {
        ProtobufWkt::Arena arena;
        printExprPtrAndType(*hash_entry.second, "in evaluateAttributes");
        const auto result = hash_entry.second->expression_ptr_.get()->Evaluate(*activation, &arena);
        if (!result.ok()) {
          // TODO: Stats?
          continue;
        }

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

  // TODO: delete
  const ExpressionPtrWithExpr& getExprPtr(std::string matcher) const {
    return *request_expr_.at(matcher);
  }

private:
  absl::flat_hash_map<std::string, std::unique_ptr<ExpressionPtrWithExpr>>
  initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) const;

  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;

  const absl::flat_hash_map<std::string, std::unique_ptr<ExpressionPtrWithExpr>> request_expr_;
  const absl::flat_hash_map<std::string, std::unique_ptr<ExpressionPtrWithExpr>> response_expr_;
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
