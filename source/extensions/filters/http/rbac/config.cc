#include "extensions/filters/http/rbac/config.h"

#include "envoy/config/filter/http/rbac/v2/rbac.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/rbac/rbac_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

Http::FilterFactoryCb RBACFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {

  RBACFilterConfigSharedPtr config = std::make_shared<RBACFilterConfig>(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::rbac::v2::RBAC&>(
          proto_config),
      stats_prefix, context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<RBACFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
RBACFilterConfigFactory::createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                                         Server::Configuration::FactoryContext&) {
  return std::make_shared<const Filters::Common::RBAC::RBACEngineImpl>(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::rbac::v2::RBACPerRoute&>(
          proto_config));
}

/**
 * Static registration for the RBAC filter. @see RegisterFactory
 */
static Registry::RegisterFactory<RBACFilterConfigFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
