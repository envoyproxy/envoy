#include "extensions/filters/network/redis_proxy/feature/hotkey/cache/cache_factory.h"

#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace Feature {
namespace HotKey {
namespace Cache {

TEST(CacheFactoryTest, CreateCacheByCacheTypeLFU) {
  CacheSharedPtr lfu =
      CacheFactory::createCache(envoy::extensions::filters::network::redis_proxy::v3::
                                    RedisProxy_FeatureConfig_HotKey_CacheType_LFU,
                                1, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->touchKey(test_key_1);
  lfu->setKey(test_key_2, 1);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  cache.clear();

  lfu->incrKey(test_key_1, 2);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(3, cache.at(test_key_1));
  cache.clear();

  lfu->attenuate();
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  lfu->reset();
  lfu->getCache(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();
}

} // namespace Cache
} // namespace HotKey
} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
