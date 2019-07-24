#pragma once

#include "envoy/config/filter/http/abac/v2alpha/abac.pb.h"
#include "envoy/config/filter/http/abac/v2alpha/abac.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ABACFilter {

/*
 * Config registration for the ABAC filter. @see NamedHttpFilterConfigFactory.
 */
class AttributeBasedAccessControlFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::http::abac::v2alpha::ABAC> {
public:
  AttributeBasedAccessControlFilterConfigFactory() : FactoryBase(HttpFilterNames::get().Abac) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::abac::v2alpha::ABAC& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override;
};

} // namespace ABACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
