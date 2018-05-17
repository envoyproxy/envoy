#pragma once

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Config registration for jwt_authn filter.
 */
class FilterFactory : public Common::FactoryBase<
                          ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication> {
public:
  FilterFactory() : FactoryBase(HttpFilterNames::get().JWT_AUTHN) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
