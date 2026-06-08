#pragma once

#include <functional>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class CELFormatter : public ::Envoy::Formatter::FormatterProvider {
public:
  CELFormatter(const ::Envoy::LocalInfo::LocalInfo& local_info,
               Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
               const cel::expr::Expr& input_expr, absl::optional<size_t>& max_length, bool typed);

  absl::optional<std::string> format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo&) const override;
  Protobuf::Value formatValue(const Envoy::Formatter::Context& context,
                              const StreamInfo::StreamInfo&) const override;

private:
  const ::Envoy::LocalInfo::LocalInfo& local_info_;
  const absl::optional<size_t> max_length_;
  const Extensions::Filters::Common::Expr::CompiledExpression compiled_expr_;
  const bool typed_;
};

class CELFormatterCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  CELFormatterCommandParser() = default;
  CELFormatterCommandParser(
      const ::Envoy::LocalInfo::LocalInfo& local_info,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder);
  ::Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                                 absl::string_view subcommand,
                                                 absl::optional<size_t> max_length) const override;

private:
  struct ConfiguredState {
    std::reference_wrapper<const ::Envoy::LocalInfo::LocalInfo> local_info;
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder;
  };

  // Present only for parsers created from explicit `envoy.formatter.cel` config. Keeping the
  // server-owned values together avoids a half-configured parser; absence means built-in parser
  // mode, where `parse()` resolves the active server context for each CEL command.
  absl::optional<ConfiguredState> configured_state_;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
