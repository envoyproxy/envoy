#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include <memory>

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr>
ExpressionManager::initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers) {
  absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr> expressions;
#if defined(USE_CEL_PARSER)
  for (const auto& matcher : matchers) {
    auto parse_status = google::api::expr::parser::Parse(matcher);
    if (!parse_status.ok()) {
      throw EnvoyException("Unable to parse descriptor expression: " +
                           parse_status.status().ToString());
    }

    auto& iter = expr_list_.emplace_back(parse_status.value());

    Filters::Common::Expr::ExpressionPtr expression =
        Extensions::Filters::Common::Expr::createExpression(builder_->builder(), iter.expr());

    expressions.try_emplace(matcher, std::move(expression));
  }
#else
  ENVOY_LOG(warn, "CEL expression parsing is not available for use in this environment."
                  " Attempted to parse " +
                      std::to_string(matchers.size()) + " expressions");
#endif
  return expressions;
}

const absl::optional<ProtobufWkt::Struct> ExpressionManager::evaluateAttributes(
    const Filters::Common::Expr::ActivationPtr& activation,
    const absl::flat_hash_map<std::string, Filters::Common::Expr::ExpressionPtr>& expr) {
  absl::optional<ProtobufWkt::Struct> proto;
  if (!expr.empty()) {
    proto.emplace(ProtobufWkt::Struct{});
    for (const auto& hash_entry : expr) {
      ProtobufWkt::Arena arena;
      const auto result = hash_entry.second->Evaluate(*activation, &arena);
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

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
