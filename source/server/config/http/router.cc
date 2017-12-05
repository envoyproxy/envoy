#include "server/config/http/router.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/json/config_schemas.h"
#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

#include "api/filter/http/router.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
RouterFilterConfig::createFilter(const envoy::api::v2::filter::http::Router& proto_config,
                                 const std::string& stat_prefix, FactoryContext& context) {
  Router::FilterConfigSharedPtr filter_config(new Router::FilterConfig(
      stat_prefix, context,
      Router::ShadowWriterPtr{new Router::ShadowWriterImpl(context.clusterManager())},
      proto_config));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*filter_config));
  };
}

HttpFilterFactoryCb RouterFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  envoy::api::v2::filter::http::Router proto_config;
  Config::FilterJson::translateRouter(json_config, proto_config);
  return createFilter(proto_config, stat_prefix, context);
}

HttpFilterFactoryCb
RouterFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                 const std::string& stat_prefix,
                                                 FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::http::Router&>(proto_config),
      stat_prefix, context);
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
