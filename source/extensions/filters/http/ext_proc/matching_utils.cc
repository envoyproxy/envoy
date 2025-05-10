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

    // Convert to proper Expr type for the new API
    std::string serialized_expr;
    if (!parse_status.value().expr().SerializeToString(&serialized_expr)) {
      throw EnvoyException("Failed to serialize expression");
    }

    google::api::expr::v1alpha1::Expr v1alpha1_expr;
    if (!v1alpha1_expr.ParseFromString(serialized_expr)) {
      throw EnvoyException("Failed to parse expression into v1alpha1 format");
    }

    // Also need to convert source info properly
    std::string serialized_source_info;
    if (!parse_status.value().source_info().SerializeToString(&serialized_source_info)) {
      throw EnvoyException("Failed to serialize source info");
    }

    google::api::expr::v1alpha1::SourceInfo v1alpha1_source_info;
    if (!v1alpha1_source_info.ParseFromString(serialized_source_info)) {
      throw EnvoyException("Failed to parse source info into v1alpha1 format");
    }

    Filters::Common::Expr::ExpressionPtr expression =
        Extensions::Filters::Common::Expr::createExpression(builder_->builder(), v1alpha1_expr);

    // Create a v1alpha1 ParsedExpr to store
    google::api::expr::v1alpha1::ParsedExpr v1alpha1_parsed_expr;
    v1alpha1_parsed_expr.mutable_expr()->CopyFrom(v1alpha1_expr);
    v1alpha1_parsed_expr.mutable_source_info()->CopyFrom(v1alpha1_source_info);

    expressions.emplace(
        matcher, ExpressionManager::CelExpression{v1alpha1_parsed_expr, std::move(expression)});
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
    header_matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher, context));
  }
  return header_matchers;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
