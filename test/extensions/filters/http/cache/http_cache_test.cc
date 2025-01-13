#include "source/extensions/filters/http/cache/http_cache.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {

TEST(HttpCacheTest, StableHashKey) {
  Key key;
  key.set_host("example.com");
  ASSERT_EQ(stableHashKey(key), 6153940628716543519u);
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
