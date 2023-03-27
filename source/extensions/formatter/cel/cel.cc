#include "source/extensions/formatter/cel/cel.h"

#include <string>

#include "source/common/config/metadata.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace Expr = Envoy::Extensions::Filters::Common::Expr;

CELFormatter::CELFormatter(Expr::Builder& builder,
                           const google::api::expr::v1alpha1::Expr& input_expr)
    : parsed_expr_(input_expr) {
  compiled_expr_ = Expr::createExpression(builder, parsed_expr_);
}

absl::optional<std::string> CELFormatter::format(const Http::RequestHeaderMap& request_headers,
                                                 const Http::ResponseHeaderMap& response_headers,
                                                 const Http::ResponseTrailerMap& response_trailers,
                                                 const StreamInfo::StreamInfo& stream_info,
                                                 absl::string_view) const {
  Protobuf::Arena arena;
  auto eval_status = Expr::evaluate(*compiled_expr_, arena, stream_info, &request_headers,
                                    &response_headers, &response_trailers);
  if (!eval_status.has_value() || eval_status.value().IsError()) {
    return "";
  }
  auto result = eval_status.value();
  return Expr::print(result);
}

ProtobufWkt::Value CELFormatter::formatValue(const Http::RequestHeaderMap& request_headers,
                                             const Http::ResponseHeaderMap& response_headers,
                                             const Http::ResponseTrailerMap& response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             absl::string_view) const {
  Protobuf::Arena arena;
  auto eval_status = Expr::evaluate(*compiled_expr_, arena, stream_info, &request_headers,
                                    &response_headers, &response_trailers);
  if (!eval_status.has_value() || eval_status.value().IsError()) {
    return ValueUtil::nullValue();
  }

  auto result = eval_status.value();
  auto str = Expr::print(result);
  return ValueUtil::stringValue(str);
}

::Envoy::Formatter::FormatterProviderPtr
CELFormatterCommandParser::parse(const std::string& command, const std::string& subcommand,
                                 absl::optional<size_t>&) const {
  if (command == "CEL") {
    auto parse_status = google::api::expr::parser::Parse(subcommand);
    if (!parse_status.ok()) {
      throw EnvoyException("Not able to parse filter expression: " +
                           parse_status.status().ToString());
    }

    auto expr_builder_ = Expr::createBuilder(nullptr);
    return std::make_unique<CELFormatter>(*expr_builder_, parse_status.value().expr());
  }

  return nullptr;
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
