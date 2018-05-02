#include "extensions/filters/http/squash/config.h"

#include "envoy/config/filter/http/squash/v2/squash.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/squash/squash_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

Http::FilterFactoryCb
SquashFilterConfigFactory::createFilterFactory(const Envoy::Json::Object& json_config,
                                               const std::string&,
                                               Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::squash::v2::Squash proto_config;
  Config::FilterJson::translateSquashConfig(json_config, proto_config);

  return createFilter(proto_config, context);
}

Http::FilterFactoryCb SquashFilterConfigFactory::createFilterFactoryFromProto(
    const Envoy::Protobuf::Message& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  return createFilter(Envoy::MessageUtil::downcastAndValidate<
                          const envoy::config::filter::http::squash::v2::Squash&>(proto_config),
                      context);
}

Http::FilterFactoryCb SquashFilterConfigFactory::createFilter(
    const envoy::config::filter::http::squash::v2::Squash& proto_config,
    Server::Configuration::FactoryContext& context) {

  SquashFilterConfigSharedPtr config = std::make_shared<SquashFilterConfig>(
      SquashFilterConfig(proto_config, context.clusterManager()));

  return [&context, config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<SquashFilter>(config, context.clusterManager()));
  };
}

/**
 * Static registration for the squash filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<SquashFilterConfigFactory,
                                        Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
