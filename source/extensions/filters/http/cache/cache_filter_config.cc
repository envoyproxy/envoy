#include "source/extensions/filters/http/cache/cache_filter_config.h"

#include "envoy/http/header_map.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cacheability_utils.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

CacheRouteFilterConfig::CacheRouteFilterConfig(HttpCachePtr cache,
                                               const CacheFilterConfigPb& config)
    : cache_(cache), pb_config_(config) {}

HttpCachePtr CacheRouteFilterConfig::getCache() const { return cache_; }

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
