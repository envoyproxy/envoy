#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/router/router.h"

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using CacheFilterConfigPb = envoy::extensions::filters::http::cache::v3::CacheConfig;
using CacheFilterConfigPbRef = std::reference_wrapper<const CacheFilterConfigPb>;

/**
 * Route configuration for the HTTP cache filter.
 */
class CacheRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit CacheRouteFilterConfig(HttpCacheSharedPtr cache, const CacheFilterConfigPb& config)
      : cache_(cache), pb_config_(config) {}

  const CacheFilterConfigPb& proto() const { return pb_config_; }
  HttpCacheSharedPtr getCache() const { return cache_; }

private:
  HttpCacheSharedPtr cache_;
  CacheFilterConfigPbRef pb_config_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
