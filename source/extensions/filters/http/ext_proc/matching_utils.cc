#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include <memory>

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

absl::flat_hash_map<std::string, ExpressionManager::CelExpression>
ExpressionManager::initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) {
  absl::flat_hash_map<std::string, ExpressionManager::CelExpression> expressions;
#if defined(USE_CEL_PARSER)
  for (const auto& matcher : matchers) {
    if (expressions.contains(matcher)) {
      continue;
    }
    auto parse_status = google::api::expr::parser::Parse(matcher);
    if (!parse_status.ok()) {
      throw EnvoyException("Unable to parse descriptor expression: " +
                           parse_status.status().ToString());
    }

    Filters::Common::Expr::ExpressionPtr expression =
        Extensions::Filters::Common::Expr::createExpression(builder_->builder(),
                                                            parse_status.value().expr());

    expressions.emplace(
        matcher, ExpressionManager::CelExpression{parse_status.value(), std::move(expression)});
  }
#else
  ENVOY_LOG(warn, "CEL expression parsing is not available for use in this environment."
                  " Attempted to parse " +
                      std::to_string(matchers.size()) + " expressions");
#endif
  return expressions;
}

ProtobufWkt::Struct
ExpressionManager::evaluateAttributes(const Filters::Common::Expr::Activation& activation,
                                      const absl::flat_hash_map<std::string, CelExpression>& expr) {

  ProtobufWkt::Struct proto;

  if (expr.empty()) {
    return proto;
  }

  for (const auto& hash_entry : expr) {
    ProtobufWkt::Arena arena;
    const auto result = hash_entry.second.compiled_expr_->Evaluate(activation, &arena);
    if (!result.ok()) {
      // TODO: Stats?
      continue;
    }

    if (result.value().IsError()) {
      ENVOY_LOG(trace, "error parsing cel expression {}", hash_entry.first);
      continue;
    }

    // Possible types from source/extensions/filters/common/expr/context.cc:
    // - String
    // - StringView
    // - Map
    // - Timestamp
    // - Int64
    // - Duration
    // - Bool
    // - Uint64
    // - Bytes
    //
    // Of these, we need to handle
    // - bool as bool
    // - int64, uint64 as number
    // - everything else as string via print
    //
    // Handling all value types here would be graceful but is not currently
    // testable and drives down coverage %. This is not a _great_ reason to
    // not do it; will get feedback from reviewers.
    ProtobufWkt::Value value;
    switch (result.value().type()) {
    case google::api::expr::runtime::CelValue::Type::kBool:
      value.set_bool_value(result.value().BoolOrDie());
      break;
    case google::api::expr::runtime::CelValue::Type::kInt64:
      value.set_number_value(result.value().Int64OrDie());
      break;
    case google::api::expr::runtime::CelValue::Type::kUint64:
      value.set_number_value(result.value().Uint64OrDie());
      break;
    default:
      value.set_string_value(Filters::Common::Expr::print(result.value()));
    }

    auto proto_mut_fields = proto.mutable_fields();
    (*proto_mut_fields)[hash_entry.first] = value;
  }

  return proto;
}

std::vector<Matchers::StringMatcherPtr>
initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list,
                   Server::Configuration::CommonFactoryContext& context) {
  std::vector<Matchers::StringMatcherPtr> header_matchers;
  for (const auto& matcher : header_list.patterns()) {
    header_matchers.push_back(
        std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
            matcher, context));
  }
  return header_matchers;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
