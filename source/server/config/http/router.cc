#include "server/config/http/router.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/json/config_schemas.h"
#include "common/protobuf/utility.h"
#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RouterFilterConfig::createRouterFilter(bool dynamic_stats,
                                                           bool start_child_span,
                                                           const std::string& stat_prefix,
                                                           FactoryContext& context) {
  Router::FilterConfigSharedPtr config(new Router::FilterConfig(
      stat_prefix, context.localInfo(), context.scope(), context.clusterManager(),
      context.runtime(), context.random(),
      Router::ShadowWriterPtr{new Router::ShadowWriterImpl(context.clusterManager())},
      dynamic_stats, start_child_span));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*config));
  };
}

HttpFilterFactoryCb RouterFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  json_config.validateSchema(Json::Schema::ROUTER_HTTP_FILTER_SCHEMA);
  return createRouterFilter(json_config.getBoolean("dynamic_stats", true),
                            json_config.getBoolean("start_child_span", false), stat_prefix,
                            context);
}

HttpFilterFactoryCb RouterFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stat_prefix, FactoryContext& context) {
  const envoy::api::v2::filter::http::Router& router =
      dynamic_cast<const envoy::api::v2::filter::http::Router&>(config);
  bool dynamic_stats = PROTOBUF_GET_WRAPPED_OR_DEFAULT(router, dynamic_stats, true);
  bool start_child_span = router.start_child_span();
  return createRouterFilter(dynamic_stats, start_child_span, stat_prefix, context);
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
