#pragma once

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

inline constexpr absl::string_view FilterName = "envoy.filters.http.rate_limit_quota";

class RateLimitQuotaFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig,
          envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaOverride>,
      public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  RateLimitQuotaFilterFactory() : FactoryBase(std::string(FilterName)) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig&
          filter_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
