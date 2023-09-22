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
  CELFormatter(Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr,
               const google::api::expr::v1alpha1::Expr&, absl::optional<size_t>&);

  absl::optional<std::string>
  formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                    const StreamInfo::StreamInfo&) const override;
  ProtobufWkt::Value formatValueWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo&) const override;

private:
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr expr_builder_;
  const google::api::expr::v1alpha1::Expr parsed_expr_;
  const absl::optional<size_t> max_length_;
  Extensions::Filters::Common::Expr::ExpressionPtr compiled_expr_;
};

class CELFormatterCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  CELFormatterCommandParser(Server::Configuration::CommonFactoryContext& context)
      : expr_builder_(Extensions::Filters::Common::Expr::getBuilder(context)){};
  ::Envoy::Formatter::FormatterProviderPtr parse(const std::string& command,
                                                 const std::string& subcommand,
                                                 absl::optional<size_t>& max_length) const override;

private:
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr expr_builder_;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
