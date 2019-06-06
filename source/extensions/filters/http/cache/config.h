#pragma once

#include "envoy/config/filter/http/cache/v2alpha/cache.pb.h"

#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::cache::v2alpha::Cache> {
public:
  CacheFilterFactory() : FactoryBase("envoy.filters.http.cache") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::cache::v2alpha::Cache& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
