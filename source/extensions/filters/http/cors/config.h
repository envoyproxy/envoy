#pragma once

#include "envoy/config/filter/http/cors/v2/cors.pb.h"
#include "envoy/config/filter/http/cors/v2/cors.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterFactory : public Common::FactoryBase<envoy::config::filter::http::cors::v2::Cors> {
public:
  CorsFilterFactory() : FactoryBase(HttpFilterNames::get().Cors) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::cors::v2::Cors& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
