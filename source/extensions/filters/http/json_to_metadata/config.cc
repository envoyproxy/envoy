#include "source/extensions/filters/http/json_to_metadata/config.h"

#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/json_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

absl::StatusOr<Http::FilterFactoryCb> JsonToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  absl::StatusOr<std::shared_ptr<FilterConfig>> filter_config_or = FilterConfig::create(
      proto_config, context.scope(), context.serverFactoryContext().regexEngine(), false);
  RETURN_IF_ERROR(filter_config_or.status());

  return [filter_config = std::move(filter_config_or.value())](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
JsonToMetadataConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return FilterConfig::create(config, context.scope(), context.regexEngine(), true);
}

/**
 * Static registration for this filter. @see RegisterFactory.
 */
REGISTER_FACTORY(JsonToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
