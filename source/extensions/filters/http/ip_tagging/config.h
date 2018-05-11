#pragma once

#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class IpTaggingFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::ip_tagging::v2::IPTagging> {
public:
  IpTaggingFilterFactory() : FactoryBase(HttpFilterNames::get().IP_TAGGING) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::ip_tagging::v2::IPTagging& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
