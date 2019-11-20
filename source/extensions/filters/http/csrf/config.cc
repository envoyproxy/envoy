#include "extensions/filters/http/csrf/config.h"

#include "envoy/config/filter/http/csrf/v2/csrf.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/csrf/csrf_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

Http::FilterFactoryCb CsrfFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  CsrfFilterConfigSharedPtr config =
      std::make_shared<CsrfFilterConfig>(policy, stats_prefix, context.scope(), context.runtime());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CsrfFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CsrfFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const Csrf::CsrfPolicy>(policy, context.runtime());
}

/**
 * Static registration for the CSRF filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CsrfFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
