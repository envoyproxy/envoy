#include "extensions/filters/http/csrf/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/csrf/csrf_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

Http::FilterFactoryCb
CsrfFilterFactory::createFilter(const std::string& stats_prefix,
                                Server::Configuration::FactoryContext& context) {
  CsrfFilterConfigSharedPtr config =
      std::make_shared<CsrfFilterConfig>(stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CsrfFilter>(config));
  };
}

/**
 * Static registration for the CSRF filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<CsrfFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
