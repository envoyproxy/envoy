#pragma once

#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.h"
#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

class HealthCheckFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::health_check::v3::HealthCheck> {
public:
  HealthCheckFilterConfig() : FactoryBase("envoy.filters.http.health_check") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::health_check::v3::HealthCheck& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
