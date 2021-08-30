#pragma once

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

/**
 * Config registration for the OnDemand filter. @see NamedHttpFilterConfigFactory.
 */
class OnDemandFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::on_demand::v3::OnDemand> {
public:
  OnDemandFilterFactory() : FactoryBase("envoy.filters.http.on_demand") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override;
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
