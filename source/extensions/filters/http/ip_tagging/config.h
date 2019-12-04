#pragma once

#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h" // For proto descriptors only
#include "envoy/config/filter/http/ip_tagging/v3alpha/ip_tagging.pb.h"
#include "envoy/config/filter/http/ip_tagging/v3alpha/ip_tagging.pb.validate.h"

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
    : public Common::FactoryBase<envoy::config::filter::http::ip_tagging::v3alpha::IPTagging> {
public:
  IpTaggingFilterFactory() : FactoryBase(HttpFilterNames::get().IpTagging) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::ip_tagging::v3alpha::IPTagging& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
