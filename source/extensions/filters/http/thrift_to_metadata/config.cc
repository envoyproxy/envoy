#include "source/extensions/filters/http/thrift_to_metadata/config.h"

#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/thrift_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

ThriftToMetadataConfig::ThriftToMetadataConfig()
    : FactoryBase("envoy.filters.http.thrift_to_metadata") {}

Http::FilterFactoryCb ThriftToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
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
REGISTER_FACTORY(ThriftToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
