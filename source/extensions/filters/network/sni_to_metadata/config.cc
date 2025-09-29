#include "source/extensions/filters/network/sni_to_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/sni_to_metadata/filter.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

SniToMetadataFilterFactory::SniToMetadataFilterFactory()
    : Common::ExceptionFreeFactoryBase<FilterConfig>(NetworkFilterNames::get().SniToMetadata) {}

absl::StatusOr<Network::FilterFactoryCb>
SniToMetadataFilterFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& config, Server::Configuration::FactoryContext& context) {

  absl::Status creation_status = absl::OkStatus();
  ConfigSharedPtr filter_config = std::make_shared<Config>(
      config, context.serverFactoryContext().regexEngine(), creation_status);

  RETURN_IF_NOT_OK_REF(creation_status);

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(filter_config));
  };
}

REGISTER_FACTORY(SniToMetadataFilterFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
