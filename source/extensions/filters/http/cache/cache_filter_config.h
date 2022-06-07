#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using CacheFilterConfigPb = envoy::extensions::filters::http::cache::v3::CacheConfig;
using CacheFilterConfigPbRef = std::reference_wrapper<const CacheFilterConfigPb>;

// Config contains raw proto config and cache reference which is used in current filter.
// Initially 'CacheRouteFilterConfig' is created from global config and can be
// overriden with route config (see config.cc)
class CacheRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit CacheRouteFilterConfig(HttpCachePtr cache, const CacheFilterConfigPb& config);

  const CacheFilterConfigPb& proto() const { return pb_config_; }
  HttpCachePtr getCache() const;

private:
  HttpCachePtr cache_;
  CacheFilterConfigPbRef pb_config_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
