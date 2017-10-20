#include "server/config/http/router.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/json/config_schemas.h"
#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RouterFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  json_config.validateSchema(Json::Schema::ROUTER_HTTP_FILTER_SCHEMA);

  Router::FilterConfigSharedPtr config(new Router::FilterConfig(
      stat_prefix, context.localInfo(), context.scope(), context.clusterManager(),
      context.runtime(), context.random(),
      Router::ShadowWriterPtr{new Router::ShadowWriterImpl(context.clusterManager())},
      json_config.getBoolean("dynamic_stats", true),
      json_config.getBoolean("start_child_span", false)));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*config));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
