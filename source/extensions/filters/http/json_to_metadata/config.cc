#include "source/extensions/filters/http/json_to_metadata/config.h"

#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/json_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

JsonToMetadataConfig::JsonToMetadataConfig() : FactoryBase("envoy.filters.http.json_to_metadata") {}

Http::FilterFactoryCb JsonToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  std::shared_ptr<FilterConfig> config =
      std::make_shared<FilterConfig>(proto_config, context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(config));
  };
}

/**
 * Static registration for this filter. @see RegisterFactory.
 */
REGISTER_FACTORY(JsonToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
