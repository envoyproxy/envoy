#pragma once

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

/**
 * Config registration for the header-to-metadata filter. @see NamedHttpFilterConfigFactory.
 */
class HeaderToMetadataConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::header_to_metadata::v3::Config> {
public:
  HeaderToMetadataConfig() : FactoryBase(HttpFilterNames::get().HeaderToMetadata) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::header_to_metadata::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
