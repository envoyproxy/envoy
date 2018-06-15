#include "extensions/filters/http/header_to_metadata/config.h"

#include <string>

#include "envoy/config/filter/http/header_to_metadata/v2/header_to_metadata.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

Http::FilterFactoryCb HeaderToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::header_to_metadata::v2::Config& proto_config,
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
static Registry::RegisterFactory<HeaderToMetadataConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
