#include "source/extensions/filters/http/set_metadata/config.h"

#include <string>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

absl::StatusOr<Http::FilterFactoryCb> SetMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto config_or_error = Config::create(proto_config, context.scope(), stats_prefix, context);
  RETURN_IF_NOT_OK_REF(config_or_error.status());
  ConfigSharedPtr filter_config = config_or_error.value();

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
