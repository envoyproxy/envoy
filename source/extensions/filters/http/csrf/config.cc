#include "source/extensions/filters/http/csrf/config.h"

#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/csrf/csrf_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

Http::FilterFactoryCb CsrfFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& base_context) {
  Server::Configuration::ServerFactoryContext& context = base_context.getServerFactoryContext();
  CsrfFilterConfigSharedPtr config = std::make_shared<CsrfFilterConfig>(
      policy, stats_prefix, base_context.scope(), context.runtime());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CsrfFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CsrfFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
    Server::Configuration::FactoryContext& base_context, ProtobufMessage::ValidationVisitor&) {
  Server::Configuration::ServerFactoryContext& context = base_context.getServerFactoryContext();
  return std::make_shared<const Csrf::CsrfPolicy>(policy, context.runtime());
}

/**
 * Static registration for the CSRF filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CsrfFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.csrf"};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
