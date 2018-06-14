#include "config.h"

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "envoy/config/filter/http/header_to_metadata/v2/header_to_metadata.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

Http::FilterFactoryCb
HeaderToMetadataConfig::createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) {
  throw new EnvoyException("No support for the v1 API");
}

Http::FilterFactoryCb
HeaderToMetadataConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                     const std::string&,
                                                     Server::Configuration::FactoryContext&) {
  const auto& typed_config = dynamic_cast<
      const envoy::config::filter::http::header_to_metadata::v2::HeaderToMetadataConfig&>(
      proto_config);
  ConfigSharedPtr filter_config(std::make_shared<Config>(typed_config));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new HeaderToMetadataFilter(filter_config)});
  };
}

ProtobufTypes::MessagePtr HeaderToMetadataConfig::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::config::filter::http::header_to_metadata::v2::HeaderToMetadataConfig()};
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
