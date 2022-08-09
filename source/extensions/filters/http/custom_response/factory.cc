#include "source/extensions/filters/http/custom_response/factory.h"

#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

Http::FilterFactoryCb CustomResponseFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto config_ptr = std::make_shared<FilterConfig>(config, context);
  return [config_ptr, stats_prefix,
          &context](Http::FilterChainFactoryCallbacks& callbacks) mutable -> void {
    callbacks.addStreamFilter(
        std::make_shared<CustomResponseFilter>(config_ptr, context, stats_prefix));
  };
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomResponseFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
