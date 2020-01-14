#pragma once

#include "envoy/config/filter/http/on_demand/v2/on_demand.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

/**
 * Config registration for the OnDemand filter. @see NamedHttpFilterConfigFactory.
 */
class OnDemandFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::on_demand::v2::OnDemand> {
public:
  OnDemandFilterFactory() : FactoryBase(HttpFilterNames::get().OnDemand) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::on_demand::v2::OnDemand& proto_config, const std::string&,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
