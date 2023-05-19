#pragma once

#include <memory>
#include <string>
#include <variant>

// #include "eval/public/cel_expression.h"
// #include "eval/public/cel_value.h"
#include "source/extensions/filters/common/expr/evaluator.h"
// #include "google/api/expr/checked.proto.h"
// #include "third_party/cel/cpp/eval/public/base_activation.h"
#include "envoy/matcher/matcher.h"
#include "xds/type/matcher/v3/cel.pb.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Matcher {

using Envoy::Extensions::Filters::Common::Expr::StreamActivation;
using google::api::expr::runtime::CelValue;

using CelMatcher = ::xds::type::matcher::v3::CelMatcher;
using ExpressionPtr = std::unique_ptr<google::api::expr::runtime::CelExpression>;
using BaseActivationPtr = std::unique_ptr<google::api::expr::runtime::BaseActivation>;
using Builder = google::api::expr::runtime::CelExpressionBuilder;
using BuilderPtr = std::unique_ptr<Builder>;

struct CelMatchData : public CustomMatchData {
  explicit CelMatchData(StreamActivation data) : data_(std::move(data)) {}
  StreamActivation data_;
};

class CelInputMatcher : public Matcher::InputMatcher {
 public:
  // TODO(tyxia) Changed to dev::cel
  // Need to change the cel library version
  CelInputMatcher(const google::api::expr::v1alpha1::CheckedExpr& input_expr, Builder& builder)
      : checked_expr_(input_expr) {
    // TODO(tyxia) Move to .cc filter
    auto cel_expression_status = builder.CreateExpression(&checked_expr_);
    if (!cel_expression_status.ok()) {
      throw EnvoyException(
          absl::StrCat("failed to create an expression: ",
                       cel_expression_status.status().message()));
    }

    compiled_expr_ = std::move(cel_expression_status.value());
  }

  bool match(const Matcher::MatchingDataType& input) override;

  // TODO(tyxia) Formalize the validation the approach. Use fixed string for now.
  virtual absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{"cel_data_input"};
  }

 private:
  // TODO(tyxia) No need for this variable
  const google::api::expr::v1alpha1::CheckedExpr checked_expr_;
  ExpressionPtr compiled_expr_;
};

class CelInputMatcherFactory : public Matcher::InputMatcherFactory {
 public:
  Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext&) override {
    const auto& cel_matcher = dynamic_cast<const CelMatcher&>(config);
    if (expr_builder_ == nullptr) {
      // expr_builder_ = createCelBuilder(factory_context.arena);
      expr_builder_ = Extensions::Filters::Common::Expr::createBuilder(nullptr);
    }

    // return std::make_unique<CelExprMatcher>(
    //     cel_matcher.expr_match().checked_expr(), *expr_builder_);
    // TODO(tyxia) Lifetime!!!! Probably no lifetime issue at all as we don't need to store cel matcher
    return [cel_matcher = std::move(cel_matcher), this] {
      return std::make_unique<CelInputMatcher>(
          cel_matcher.expr_match().checked_expr(), *expr_builder_);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<CelMatcher>();
  }

  std::string name() const override { return "cel_input_matcher_factory"; }
private:
  BuilderPtr expr_builder_;
};

} // namespace Matcher
} // namespace Envoy