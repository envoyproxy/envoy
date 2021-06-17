#pragma once

#include "envoy/extensions/filters/http/cors/v3/cors.pb.h"
#include "envoy/extensions/filters/http/cors/v3/cors.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::cors::v3::Cors> {
public:
  CorsFilterFactory() : FactoryBase("envoy.filters.http.cors") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cors::v3::Cors& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
