#include "extensions/filters/http/cors/config.h"

#include <memory>
#include <string>

#include "envoy/config/filter/http/cors/v2/cors.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/router/config_impl.h"

#include "extensions/filters/http/cors/cors_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

Http::FilterFactoryCb
CorsFilterFactory::createFilterFactoryFromProto(const Protobuf::Message&,
                                                const std::string& stats_prefix,
                                                Server::Configuration::FactoryContext& context) {
  CorsFilterConfigSharedPtr config =
      std::make_shared<CorsFilterConfig>(stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CorsFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr CorsFilterFactory::createRouteSpecificFilterConfig(
    const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::config::filter::http::cors::v2::PerRouteCorsPolicy&>(proto_config, validator);
  return std::make_shared<const Envoy::Router::CorsPolicyImpl>(typed_config, context.runtime());
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CorsFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
