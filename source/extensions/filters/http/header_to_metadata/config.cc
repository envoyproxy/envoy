#include "source/extensions/filters/http/header_to_metadata/config.h"

#include <string>

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

absl::StatusOr<Http::FilterFactoryCb> HeaderToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  absl::StatusOr<ConfigSharedPtr> filter_config_or =
      Config::create(proto_config, context.serverFactoryContext().regexEngine(), false);
  RETURN_IF_ERROR(filter_config_or.status());

  return [filter_config = std::move(filter_config_or.value())](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new HeaderToMetadataFilter(filter_config)});
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
HeaderToMetadataConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::header_to_metadata::v3::Config& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  absl::StatusOr<ConfigSharedPtr> config_or = Config::create(config, context.regexEngine(), true);
  RETURN_IF_ERROR(config_or.status());
  return std::move(config_or.value());
}

/**
 * Static registration for the header-to-metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
