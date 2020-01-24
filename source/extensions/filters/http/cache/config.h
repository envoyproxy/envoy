#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.validate.h"

#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::cache::v3::CacheConfig> {
public:
  CacheFilterFactory() : FactoryBase(HttpFilterNames::get().Cache) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
