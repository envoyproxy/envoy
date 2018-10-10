#pragma once

#include "envoy/config/filter/http/ext_authz/v2alpha/ext_authz.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz> {
public:
  ExtAuthzFilterConfig() : FactoryBase(HttpFilterNames::get().ExtAuthorization) {}

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
