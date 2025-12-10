#include "source/extensions/filters/http/cache_v2/http_cache.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

namespace {

TEST(HttpCacheTest, StableHashKey) {
  Key key;
  key.set_host("example.com");
  ASSERT_EQ(stableHashKey(key), 2966927868601563246);
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
