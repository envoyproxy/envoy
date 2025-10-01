#pragma once

#include "envoy/extensions/filters/network/sni_to_metadata/v3/sni_to_metadata.pb.h"
#include "envoy/extensions/filters/network/sni_to_metadata/v3/sni_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

using FilterConfig = envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter;

/**
 * Config registration for the SNI to metadata filter. @see NamedNetworkFilterConfigFactory.
 */
class SniToMetadataFilterFactory : public Common::ExceptionFreeFactoryBase<FilterConfig> {
public:
  SniToMetadataFilterFactory();

private:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const FilterConfig& config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
