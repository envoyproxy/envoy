#pragma once

#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.validate.h"

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
    : public Common::FactoryBase<envoy::extensions::filters::http::csrf::v3::CsrfPolicy> {
public:
  CsrfFilterFactory() : FactoryBase(HttpFilterNames::get().Csrf) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
