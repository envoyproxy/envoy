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
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  CsrfFilterConfigSharedPtr config = std::make_shared<CsrfFilterConfig>(
      policy, stats_prefix, context.scope(), context.serverFactoryContext().runtime());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CsrfFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CsrfFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const Csrf::CsrfPolicy>(policy, context.runtime());
}

/**
 * Static registration for the CSRF filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(CsrfFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.csrf");

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
