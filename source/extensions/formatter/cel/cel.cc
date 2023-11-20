#include "source/extensions/formatter/cel/cel.h"

#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace Expr = Filters::Common::Expr;

CELFormatter::CELFormatter(Expr::BuilderInstanceSharedPtr expr_builder,
                           const google::api::expr::v1alpha1::Expr& input_expr,
                           absl::optional<size_t>& max_length)
    : expr_builder_(expr_builder), parsed_expr_(input_expr), max_length_(max_length) {
  compiled_expr_ = Expr::createExpression(expr_builder_->builder(), parsed_expr_);
}

absl::optional<std::string>
CELFormatter::formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  Protobuf::Arena arena;
  auto eval_status = Expr::evaluate(*compiled_expr_, arena, stream_info, &context.requestHeaders(),
                                    &context.responseHeaders(), &context.responseTrailers());
  if (!eval_status.has_value() || eval_status.value().IsError()) {
    return absl::nullopt;
  }
  const auto result = Expr::print(eval_status.value());
  if (max_length_) {
    return result.substr(0, max_length_.value());
  }

  return result;
}

ProtobufWkt::Value
CELFormatter::formatValueWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  auto result = formatWithContext(context, stream_info);
  if (!result.has_value()) {
    return ValueUtil::nullValue();
  }
  return ValueUtil::stringValue(result.value());
}

::Envoy::Formatter::FormatterProviderPtr
CELFormatterCommandParser::parse(const std::string& command, const std::string& subcommand,
                                 absl::optional<size_t>& max_length) const {
#if defined(USE_CEL_PARSER)
  if (command == "CEL") {
    auto parse_status = google::api::expr::parser::Parse(subcommand);
    if (!parse_status.ok()) {
      throw EnvoyException("Not able to parse filter expression: " +
                           parse_status.status().ToString());
    }

    return std::make_unique<CELFormatter>(expr_builder_, parse_status.value().expr(), max_length);
  }

  return nullptr;
#else
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
