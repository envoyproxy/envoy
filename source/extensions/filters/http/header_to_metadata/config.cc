#include "extensions/filters/http/header_to_metadata/config.h"

#include <string>

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

Http::FilterFactoryCb HeaderToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ConfigSharedPtr filter_config(std::make_shared<Config>(proto_config));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new HeaderToMetadataFilter(filter_config)});
  };
}

/**
 * Static registration for the header-to-metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
