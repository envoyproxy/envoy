#pragma once

#include "envoy/config/filter/http/header_to_metadata/v2/header_to_metadata.pb.h"

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
    : public Common::FactoryBase<envoy::config::filter::http::header_to_metadata::v2::Config> {
public:
  HeaderToMetadataConfig() : FactoryBase(HttpFilterNames::get().HEADER_TO_METADATA) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::header_to_metadata::v2::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
