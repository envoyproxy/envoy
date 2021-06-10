#include "extensions/filters/network/redis_proxy/hotkey/cache/cache_factory.h"

#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {
namespace Cache {

class CacheImplTest : public Cache {
public:
  CacheImplTest(const uint8_t& capacity, const uint8_t& warming_capacity)
      : Cache(capacity, warming_capacity) {}
  ~CacheImplTest() override = default;

  void reset() override {}
  void touchKey(const std::string&) override {}
  void incrKey(const std::string&, const uint32_t&) override {}
  void setKey(const std::string&, const uint32_t&) override {}
  uint8_t getCache(absl::flat_hash_map<std::string, uint32_t>&) override { return 0; }
  void attenuate(const uint64_t&) override {}
};
using CacheImplTestPtr = std::shared_ptr<CacheImplTest>;

TEST(CacheTest, CacheImplTest) {
  CacheImplTestPtr cache = std::make_shared<CacheImplTest>(1, 1);
  EXPECT_EQ(true, bool(cache));
}

TEST(CacheFactoryTest, CreateCacheByCacheTypeLFU) {
  CacheSharedPtr lfu =
      CacheFactory::createCache(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                    HotKey::CacheType::RedisProxy_HotKey_CacheType_LFU,
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
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
