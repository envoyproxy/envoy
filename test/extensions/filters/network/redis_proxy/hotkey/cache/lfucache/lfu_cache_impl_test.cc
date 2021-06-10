#include "extensions/filters/network/redis_proxy/hotkey/cache/lfucache/lfu_cache.h"

#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {
namespace Cache {
namespace LFUCache {

TEST(LFUCacheTest, Reset) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(1, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->touchKey(test_key_1);
  lfu->touchKey(test_key_2);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(1, cache.at(test_key_2));
  cache.clear();

  lfu->reset();
  lfu->getCache(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  lfu->touchKey(test_key_2);
  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();
}

TEST(LFUCacheTest, TouchKey) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(3, 2);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2"), test_key_3("test_key_3"),
      test_key_4("test_key_4"), test_key_5("test_key_5"), test_key_6("test_key_6");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  cache.clear();

  lfu->touchKey(test_key_2);
  lfu->touchKey(test_key_3);
  lfu->touchKey(test_key_4);
  lfu->touchKey(test_key_5);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(1, cache.at(test_key_4));
  EXPECT_EQ(1, cache.count(test_key_5));
  EXPECT_EQ(1, cache.at(test_key_5));
  cache.clear();

  lfu->touchKey(test_key_5);
  lfu->touchKey(test_key_4);
  lfu->touchKey(test_key_3);
  lfu->touchKey(test_key_2);
  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(3, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  cache.clear();

  lfu->touchKey(test_key_1);
  lfu->touchKey(test_key_2);
  lfu->touchKey(test_key_4);
  lfu->touchKey(test_key_6);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(4, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(3, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(3, cache.at(test_key_4));
  cache.clear();

  lfu->touchKey(test_key_1);
  lfu->touchKey(test_key_6);
  lfu->touchKey(test_key_6);
  lfu->touchKey(test_key_6);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(3, cache.at(test_key_4));
  EXPECT_EQ(1, cache.count(test_key_6));
  EXPECT_EQ(4, cache.at(test_key_6));
  cache.clear();
}

TEST(LFUCacheTest, IncrKey) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(1, 1);
  std::string test_key_1("test_key_1");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->setKey(test_key_1, 0);
  lfu->getCache(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  lfu->setKey(test_key_1, 5);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  cache.clear();

  lfu->incrKey(test_key_1, 5);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(10, cache.at(test_key_1));
  cache.clear();

  lfu->incrKey(test_key_1, UINT32_MAX - 1);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(UINT32_MAX, cache.at(test_key_1));
  cache.clear();
}

TEST(LFUCacheTest, SetKey) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(1, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->setKey(test_key_1, 0);
  lfu->getCache(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  lfu->setKey(test_key_1, 5);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  cache.clear();

  lfu->setKey(test_key_2, UINT32_MAX);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(UINT32_MAX, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_2, 0);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  cache.clear();

  lfu->setKey(test_key_1, 5);
  lfu->setKey(test_key_2, 5);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(5, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_2, 5);
  lfu->setKey(test_key_1, 5);
  lfu->getCache(cache);
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  cache.clear();
}

TEST(LFUCacheTest, GetCache) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(UINT8_MAX, UINT8_MAX);
  std::string test_key_1("test_key_1");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->touchKey(test_key_1);
  EXPECT_EQ(1, lfu->getCache(cache));
  EXPECT_EQ(1, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(1, cache.at(test_key_1));
  cache.clear();

  for (uint16_t i = 0; i < (2 * UINT8_MAX); ++i) {
    lfu->touchKey(std::to_string(i));
  }
  EXPECT_EQ(UINT8_MAX, lfu->getCache(cache));
  EXPECT_EQ(UINT8_MAX, cache.size());
  EXPECT_EQ(0, cache.count(test_key_1));
  cache.clear();
}

TEST(LFUCacheTest, TouchAndSetKey) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(3, 2);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2"), test_key_3("test_key_3"),
      test_key_4("test_key_4"), test_key_5("test_key_5");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->setKey(test_key_1, 5);
  lfu->touchKey(test_key_2);
  lfu->touchKey(test_key_3);
  lfu->touchKey(test_key_4);
  lfu->touchKey(test_key_5);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(5, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(1, cache.at(test_key_4));
  EXPECT_EQ(1, cache.count(test_key_5));
  EXPECT_EQ(1, cache.at(test_key_5));
  cache.clear();

  lfu->touchKey(test_key_5);
  lfu->touchKey(test_key_4);
  lfu->touchKey(test_key_3);
  lfu->touchKey(test_key_2);
  lfu->setKey(test_key_1, 1);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(2, cache.at(test_key_4));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  cache.clear();

  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  cache.clear();

  lfu->setKey(test_key_1, 0);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_4));
  EXPECT_EQ(2, cache.at(test_key_4));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_1, UINT32_MAX);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(UINT32_MAX, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  cache.clear();

  lfu->touchKey(test_key_1);
  lfu->getCache(cache);
  EXPECT_EQ(3, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(UINT32_MAX, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  cache.clear();
}

TEST(LFUCacheTest, Attenuate) {
  std::unique_ptr<LFUCache> lfu = std::make_unique<LFUCache>(2, 1);
  std::string test_key_1("test_key_1"), test_key_2("test_key_2"), test_key_3("test_key_3");
  absl::flat_hash_map<std::string, uint32_t> cache;

  lfu->setKey(test_key_1, 10);
  lfu->setKey(test_key_2, 5);
  lfu->attenuate();
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(10 >> 1, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(5 >> 1, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_1, 2);
  lfu->attenuate();
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(2 >> 1, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(5 >> 2, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_1, 1);
  lfu->attenuate();
  lfu->getCache(cache);
  EXPECT_EQ(0, cache.size());
  cache.clear();

  lfu->setKey(test_key_1, 4);
  lfu->setKey(test_key_2, 2);
  absl::SleepFor(absl::Milliseconds(3));
  lfu->touchKey(test_key_2);
  lfu->attenuate(2);
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(4 >> 1, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(3, cache.at(test_key_2));
  cache.clear();

  absl::SleepFor(absl::Milliseconds(3));
  lfu->attenuate(2);
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_1));
  EXPECT_EQ(4 >> 2, cache.at(test_key_1));
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(3 >> 1, cache.at(test_key_2));
  cache.clear();

  lfu->setKey(test_key_1, 4);
  lfu->setKey(test_key_2, 4);
  lfu->attenuate();
  lfu->setKey(test_key_3, 4 >> 1);
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(4 >> 1, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(4 >> 1, cache.at(test_key_3));
  cache.clear();

  lfu->setKey(test_key_1, 2);
  lfu->setKey(test_key_2, 2);
  lfu->setKey(test_key_3, 2);
  lfu->attenuate(10);
  lfu->getCache(cache);
  EXPECT_EQ(2, cache.size());
  EXPECT_EQ(1, cache.count(test_key_2));
  EXPECT_EQ(2, cache.at(test_key_2));
  EXPECT_EQ(1, cache.count(test_key_3));
  EXPECT_EQ(2, cache.at(test_key_3));
  cache.clear();
}

} // namespace LFUCache
} // namespace Cache
} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
