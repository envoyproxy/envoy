#pragma once

#include "envoy/config/filter/http/fault/v2/fault.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * Config registration for the fault injection filter. @see NamedHttpFilterConfigFactory.
 */
class FaultFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::fault::v2::HTTPFault> {
public:
  FaultFilterFactory() : FactoryBase(HttpFilterNames::get().FAULT) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::fault::v2::HTTPFault& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::fault::v2::HTTPFault& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
