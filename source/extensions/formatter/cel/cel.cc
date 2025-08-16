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

CELFormatter::CELFormatter(const ::Envoy::LocalInfo::LocalInfo& local_info,
                           Expr::BuilderInstanceSharedPtr expr_builder,
                           const cel::expr::Expr& input_expr, absl::optional<size_t>& max_length)
    : local_info_(local_info), expr_builder_(expr_builder), parsed_expr_(input_expr),
      max_length_(max_length) {
  compiled_expr_ = Expr::createExpression(expr_builder_->builder(), parsed_expr_);
}

absl::optional<std::string>
CELFormatter::formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  Protobuf::Arena arena;
  auto eval_status =
      Expr::evaluate(*compiled_expr_, arena, &local_info_, stream_info, &context.requestHeaders(),
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

Protobuf::Value
CELFormatter::formatValueWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  auto result = formatWithContext(context, stream_info);
  if (!result.has_value()) {
    return ValueUtil::nullValue();
  }
  return ValueUtil::stringValue(result.value());
}

::Envoy::Formatter::FormatterProviderPtr
CELFormatterCommandParser::parse(absl::string_view command, absl::string_view subcommand,
                                 absl::optional<size_t> max_length) const {
#if defined(USE_CEL_PARSER)
  if (command == "CEL") {
    auto parse_status = google::api::expr::parser::Parse(subcommand);
    if (!parse_status.ok()) {
      throw EnvoyException("Not able to parse filter expression: " +
                           parse_status.status().ToString());
    }

    Server::Configuration::ServerFactoryContext& context =
        Server::Configuration::ServerFactoryContextInstance::get();

    const auto& parsed_expr = parse_status.value();
    std::string serialized_expr;
    if (!parsed_expr.expr().SerializeToString(&serialized_expr)) {
      throw EnvoyException("Failed to serialize expression");
    }

    cel::expr::Expr cel_expr;
    if (!cel_expr.ParseFromString(serialized_expr)) {
      throw EnvoyException("Failed to parse expression into cel::expr::Expr format");
    }

    return std::make_unique<CELFormatter>(context.localInfo(),
                                          Extensions::Filters::Common::Expr::getBuilder(context),
                                          cel_expr, max_length);
  }

  return nullptr;
#else
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
