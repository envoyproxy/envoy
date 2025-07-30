#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class CELFormatter : public ::Envoy::Formatter::FormatterProvider {
public:
  CELFormatter(const ::Envoy::LocalInfo::LocalInfo& local_info,
               Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr, const cel::expr::Expr&,
               absl::optional<size_t>&);

  absl::optional<std::string>
  formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                    const StreamInfo::StreamInfo&) const override;
  ProtobufWkt::Value formatValueWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo&) const override;

private:
  const ::Envoy::LocalInfo::LocalInfo& local_info_;
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr expr_builder_;
  const cel::expr::Expr parsed_expr_;
  const absl::optional<size_t> max_length_;
  Extensions::Filters::Common::Expr::ExpressionPtr compiled_expr_;
};

class CELFormatterCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  CELFormatterCommandParser() = default;
  ::Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                                 absl::string_view subcommand,
                                                 absl::optional<size_t> max_length) const override;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
