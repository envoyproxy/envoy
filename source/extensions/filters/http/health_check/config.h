#pragma once

#include "envoy/config/filter/http/health_check/v2/health_check.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

class HealthCheckFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::health_check::v2::HealthCheck> {
public:
  HealthCheckFilterConfig() : FactoryBase(HttpFilterNames::get().HealthCheck) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string&,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
