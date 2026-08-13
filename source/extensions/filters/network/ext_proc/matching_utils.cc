#include "source/extensions/filters/network/ext_proc/matching_utils.h"

#include <memory>
#include <string>

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

absl::flat_hash_map<std::string, ExpressionManager::CelExpression>
ExpressionManager::initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers,
                                   absl::Status& creation_status) {
  absl::flat_hash_map<std::string, ExpressionManager::CelExpression> expressions;
#if defined(USE_CEL_PARSER)
  for (const auto& matcher : matchers) {
    if (!creation_status.ok()) {
      return expressions;
    }
    if (expressions.contains(matcher)) {
      continue;
    }
    const absl::StatusOr<cel::expr::ParsedExpr> parse_status =
        google::api::expr::parser::Parse(matcher);
    if (!parse_status.ok()) {
      creation_status = absl::InvalidArgumentError("Unable to parse descriptor expression: " +
                                                   parse_status.status().ToString());
      return expressions;
    }

    const auto& parsed_expr = parse_status.value();
    const cel::expr::Expr& cel_expr = parsed_expr.expr();
    auto compiled_expression =
        Extensions::Filters::Common::Expr::CompiledExpression::Create(builder_, cel_expr);
    if (!compiled_expression.ok()) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("failed to create an expression: ", compiled_expression.status().message()));
      return expressions;
    }
    expressions.emplace(matcher, std::move(compiled_expression.value()));
  }
#else
  ENVOY_LOG(warn, "CEL expression parsing is not available for use in this environment."
                  " Attempted to parse " +
                      std::to_string(matchers.size()) + " expressions");
#endif
  return expressions;
}

Protobuf::Struct
ExpressionManager::evaluateAttributes(const Filters::Common::Expr::Activation& activation,
                                      const absl::flat_hash_map<std::string, CelExpression>& expr) {
  Protobuf::Struct proto;

  if (expr.empty()) {
    return proto;
  }

  for (const auto& hash_entry : expr) {
    Protobuf::Arena arena;
    const absl::StatusOr<google::api::expr::runtime::CelValue>& result =
        hash_entry.second.evaluate(activation, &arena);
    if (!result.ok()) {
      continue;
    }

    if (result.value().IsError()) {
      ENVOY_LOG(trace, "error evaluating cel expression {}", hash_entry.first);
      continue;
    }

    Protobuf::Value value;
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

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
