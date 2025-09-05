#include "source/extensions/formatter/cel/cel.h"

#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#include "eval/public/value_export_util.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace Expr = Filters::Common::Expr;

CELFormatter::CELFormatter(const ::Envoy::LocalInfo::LocalInfo& local_info,
                           Expr::BuilderInstanceSharedConstPtr expr_builder,
                           const cel::expr::Expr& input_expr, absl::optional<size_t>& max_length,
                           bool typed)
    : local_info_(local_info), max_length_(max_length), compiled_expr_([&]() {
        auto compiled_expr = Expr::CompiledExpression::Create(expr_builder, input_expr);
        if (!compiled_expr.ok()) {
          throw EnvoyException(
              absl::StrCat("failed to create an expression: ", compiled_expr.status().message()));
        }
        return std::move(compiled_expr.value());
      }()),
      typed_(typed) {}

absl::optional<std::string>
CELFormatter::formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  Protobuf::Arena arena;
  auto eval_status =
      compiled_expr_.evaluate(arena, &local_info_, stream_info, &context.requestHeaders(),
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
  if (typed_) {
    Protobuf::Arena arena;
    auto eval_status =
        compiled_expr_.evaluate(arena, &local_info_, stream_info, &context.requestHeaders(),
                                &context.responseHeaders(), &context.responseTrailers());
    if (!eval_status.has_value() || eval_status.value().IsError()) {
      return ValueUtil::nullValue();
    }

    Protobuf::Value proto_value;
    if (!ExportAsProtoValue(eval_status.value(), &proto_value).ok()) {
      return ValueUtil::nullValue();
    }

    if (max_length_ && proto_value.kind_case() == Protobuf::Value::kStringValue) {
      proto_value.set_string_value(proto_value.string_value().substr(0, max_length_.value()));
    }
    return proto_value;
  } else {
    auto result = formatWithContext(context, stream_info);
    if (!result.has_value()) {
      return ValueUtil::nullValue();
    }
    return ValueUtil::stringValue(result.value());
  }
}

::Envoy::Formatter::FormatterProviderPtr
CELFormatterCommandParser::parse(absl::string_view command, absl::string_view subcommand,
                                 absl::optional<size_t> max_length) const {
#if defined(USE_CEL_PARSER)
  if (command == "CEL" || command == "TYPED_CEL") {
    auto parse_status = google::api::expr::parser::Parse(subcommand);
    if (!parse_status.ok()) {
      throw EnvoyException("Not able to parse expression: " + parse_status.status().ToString());
    }
    Server::Configuration::ServerFactoryContext& context =
        Server::Configuration::ServerFactoryContextInstance::get();
    return std::make_unique<CELFormatter>(
        context.localInfo(), Extensions::Filters::Common::Expr::getBuilder(context),
        parse_status.value().expr(), max_length, command == "TYPED_CEL");
  }

  return nullptr;
#else
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
