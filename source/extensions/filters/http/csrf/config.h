#pragma once

#include "envoy/config/filter/http/csrf/v2/csrf.pb.h"
#include "envoy/config/filter/http/csrf/v2/csrf.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

/**
 * Config registration for the CSRF filter. @see NamedHttpFilterConfigFactory.
 */
class CsrfFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::csrf::v2::CsrfPolicy> {
public:
  CsrfFilterFactory() : FactoryBase(HttpFilterNames::get().Csrf) {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
