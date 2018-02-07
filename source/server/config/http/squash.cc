#include "server/config/http/squash.h"

#include <string>

#include "envoy/config/filter/http/squash/v2/squash.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "common/http/filter/squash_filter.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb SquashFilterConfig::createFilterFactory(const Envoy::Json::Object& json_config,
                                                            const std::string&,
                                                            FactoryContext& context) {
  envoy::config::filter::http::squash::v2::Squash proto_config;
  Config::FilterJson::translateSquashConfig(json_config, proto_config);

  return createFilter(proto_config, context);
}

HttpFilterFactoryCb
SquashFilterConfig::createFilterFactoryFromProto(const Envoy::Protobuf::Message& proto_config,
                                                 const std::string&, FactoryContext& context) {
  return createFilter(Envoy::MessageUtil::downcastAndValidate<
                          const envoy::config::filter::http::squash::v2::Squash&>(proto_config),
                      context);
}

HttpFilterFactoryCb SquashFilterConfig::createFilter(
    const envoy::config::filter::http::squash::v2::Squash& proto_config, FactoryContext& context) {

  Http::SquashFilterConfigSharedPtr config = std::make_shared<Http::SquashFilterConfig>(
      Http::SquashFilterConfig(proto_config, context.clusterManager()));

  return [&context, config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = new Http::SquashFilter(config, context.clusterManager());
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{filter});
  };
}

/**
 * Static registration for the squash filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<SquashFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
