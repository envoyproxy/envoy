#include "extensions/filters/http/abac/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/abac/abac_filter.h"

#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ABACFilter {

Http::FilterFactoryCb
AttributeBasedAccessControlFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::abac::v2alpha::ABAC& config, const std::string&,
    Server::Configuration::FactoryContext&) {
  google::api::expr::runtime::InterpreterOptions options;

  // Conformance with java/go runtimes requires this setting
  options.partial_string_match = true;

  auto builder = google::api::expr::runtime::CreateCelExpressionBuilder(options);
  auto register_status =
      google::api::expr::runtime::RegisterBuiltinFunctions(builder->GetRegistry());
  if (!register_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register built-in functions: ", register_status.message()));
  }
  google::api::expr::v1alpha1::SourceInfo source_info;
  auto cel_expression_status = builder->CreateExpression(&config.expr(), &source_info);
  if (!cel_expression_status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to create an expression: ", cel_expression_status.status().message()));
  }
  auto cel_expression = absl::ShareUniquePtr(std::move(cel_expression_status.ValueOrDie()));
  return [cel_expression](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<AttributeBasedAccessControlFilter>(cel_expression));
  };
}

/**
 * Static registration for the ABAC filter. @see RegisterFactory
 */
REGISTER_FACTORY(AttributeBasedAccessControlFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ABACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
