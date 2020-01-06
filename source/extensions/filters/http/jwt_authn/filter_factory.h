#pragma once

#include "envoy/extensions/filters/http/jwt_authn/v3alpha/config.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3alpha/config.pb.validate.h"
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
                          envoy::extensions::filters::http::jwt_authn::v3alpha::JwtAuthentication> {
public:
  FilterFactory() : FactoryBase(HttpFilterNames::get().JwtAuthn) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::jwt_authn::v3alpha::JwtAuthentication& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
