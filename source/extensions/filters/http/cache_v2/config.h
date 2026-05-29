#pragma once

#include "envoy/extensions/filters/http/cache_v2/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache_v2/v3/cache.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::cache_v2::v3::CacheV2Config> {
public:
  CacheFilterFactory() : FactoryBase("envoy.filters.http.cache_v2") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
