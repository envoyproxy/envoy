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
    : ExceptionFreeFactoryBase("envoy.filters.http.thrift_to_metadata") {}

absl::StatusOr<Http::FilterFactoryCb> ThriftToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  std::shared_ptr<FilterConfig> config =
      std::make_shared<FilterConfig>(proto_config, context.scope(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(config));
  };
}

absl::StatusOr<Http::FilterFactoryCb> ThriftToMetadataConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
    const std::string&, Server::Configuration::ServerFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  std::shared_ptr<FilterConfig> config =
      std::make_shared<FilterConfig>(proto_config, context.scope(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

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
