#include "source/extensions/filters/http/set_metadata/config.h"

#include <string>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

Http::FilterFactoryCb SetMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ConfigSharedPtr filter_config(std::make_shared<Config>(proto_config));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new SetMetadataFilter(filter_config)});
  };
}

REGISTER_FACTORY(SetMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
