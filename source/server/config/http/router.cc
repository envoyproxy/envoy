#include "server/config/http/router.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/json/config_schemas.h"
#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

HttpFilterFactoryCb createRouterFilterFactory(const envoy::api::v2::filter::http::Router& router,
                                              const std::string& stat_prefix,
                                              FactoryContext& context) {
  Router::FilterConfigSharedPtr config(new Router::FilterConfig(
      stat_prefix, context,
      Router::ShadowWriterPtr{new Router::ShadowWriterImpl(context.clusterManager())}, router));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*config));
  };
}

} // namespace

HttpFilterFactoryCb RouterFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  envoy::api::v2::filter::http::Router router;
  Config::FilterJson::translateRouter(json_config, router);

  return createRouterFilterFactory(router, stat_prefix, context);
}

HttpFilterFactoryCb RouterFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stat_prefix, FactoryContext& context) {
  return createRouterFilterFactory(
      dynamic_cast<const envoy::api::v2::filter::http::Router&>(config), stat_prefix, context);
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
