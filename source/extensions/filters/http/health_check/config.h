#pragma once

#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.h"
#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

class HealthCheckFilterConfig
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::health_check::v3::HealthCheck> {
public:
  HealthCheckFilterConfig() : ExceptionFreeFactoryBase("envoy.filters.http.health_check") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::health_check::v3::HealthCheck& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::health_check::v3::HealthCheck& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryHelper(
      const envoy::extensions::filters::http::health_check::v3::HealthCheck& proto_config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& scope);
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
