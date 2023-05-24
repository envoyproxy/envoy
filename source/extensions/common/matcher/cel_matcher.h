#pragma once

#include <memory>
#include <string>
#include <variant>

// #include "eval/public/cel_expression.h"
// #include "eval/public/cel_value.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/common/protobuf/utility.h"
// #include "google/api/expr/checked.proto.h"
// #include "third_party/cel/cpp/eval/public/base_activation.h"
#include "envoy/matcher/matcher.h"
#include "xds/type/matcher/v3/cel.pb.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

using ::Envoy::Extensions::Filters::Common::Expr::StreamActivation;
using ::Envoy::Matcher::InputMatcher;
using ::Envoy::Matcher::InputMatcherFactory;
using ::Envoy::Matcher::InputMatcherFactoryCb;
using ::Envoy::Matcher::MatchingDataType;

using google::api::expr::runtime::CelValue;
using xds::type::v3::CelExpression;

using CelMatcher = ::xds::type::matcher::v3::CelMatcher;
using CompiledExpressionPtr = std::unique_ptr<google::api::expr::runtime::CelExpression>;
using BaseActivationPtr = std::unique_ptr<google::api::expr::runtime::BaseActivation>;
using Builder = google::api::expr::runtime::CelExpressionBuilder;
using BuilderPtr = std::unique_ptr<Builder>;

struct CelMatchData : public ::Envoy::Matcher::CustomMatchData {
  explicit CelMatchData(StreamActivation data) : data_(std::move(data)) {}
  StreamActivation data_;
};

class CelInputMatcher : public InputMatcher, public Logger::Loggable<Logger::Id::matcher> {
public:
  CelInputMatcher(const CelExpression& input_expr);

  bool match(const MatchingDataType& input) override;

  // TODO(tyxia) Formalize the validation the approach. Use fixed string for now.
  virtual absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{"cel_data_input"};
  }

private:
  // TODO(tyxia) lifetime
  BuilderPtr expr_builder_;
  CompiledExpressionPtr compiled_expr_;
};

class CelInputMatcherFactory : public InputMatcherFactory {
public:
  InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message& config,
                              Server::Configuration::ServerFactoryContext&) override {
    const auto& cel_matcher = dynamic_cast<const CelMatcher&>(config);

    return [cel_matcher = std::move(cel_matcher)] {
      return std::make_unique<CelInputMatcher>(cel_matcher.expr_match());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<CelMatcher>();
  }

  std::string name() const override { return "cel_input_matcher_factory"; }

private:
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
