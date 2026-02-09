#include "source/extensions/dynamic_modules/module_cache.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

class ModuleCacheTest : public testing::Test {
protected:
  void SetUp() override { clearModuleCacheForTesting(); }

  void TearDown() override { clearModuleCacheForTesting(); }
};

TEST_F(ModuleCacheTest, LookupMiss) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  auto result = cache.lookup("nonexistent_key", now);
  EXPECT_FALSE(result.cache_hit);
  EXPECT_FALSE(result.fetch_in_progress);
  EXPECT_EQ(result.module, nullptr);
}

TEST_F(ModuleCacheTest, MarkInProgressAndLookup) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.markInProgress("test_key", now);
  EXPECT_EQ(cache.size(), 1);

  auto result = cache.lookup("test_key", now);
  EXPECT_TRUE(result.cache_hit);
  EXPECT_TRUE(result.fetch_in_progress);
  EXPECT_EQ(result.module, nullptr);
}

TEST_F(ModuleCacheTest, UpdateWithModuleAndLookup) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.markInProgress("test_key", now);
  cache.update("test_key", "module_binary_data", now);

  auto result = cache.lookup("test_key", now);
  EXPECT_TRUE(result.cache_hit);
  EXPECT_FALSE(result.fetch_in_progress);
  ASSERT_NE(result.module, nullptr);
  EXPECT_EQ(*result.module, "module_binary_data");
}

TEST_F(ModuleCacheTest, NegativeCaching) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  // Update with empty module (failure).
  cache.update("test_key", "", now);

  // Lookup within negative cache TTL.
  auto result = cache.lookup("test_key", now);
  EXPECT_TRUE(result.cache_hit);
  EXPECT_FALSE(result.fetch_in_progress);
  EXPECT_EQ(result.module, nullptr);

  // Lookup after negative cache TTL expires (10 seconds).
  MonotonicTime after_expiry = now + std::chrono::seconds(11);
  result = cache.lookup("test_key", after_expiry);
  EXPECT_FALSE(result.cache_hit);
  EXPECT_FALSE(result.fetch_in_progress);
  EXPECT_EQ(result.module, nullptr);
}

TEST_F(ModuleCacheTest, PositiveCacheTTL) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.update("test_key", "module_binary_data", now);

  // Lookup within cache TTL (24 hours).
  MonotonicTime within_ttl = now + std::chrono::hours(23);
  auto result = cache.lookup("test_key", within_ttl);
  EXPECT_TRUE(result.cache_hit);
  ASSERT_NE(result.module, nullptr);
  EXPECT_EQ(*result.module, "module_binary_data");

  // Lookup after cache TTL expires.
  MonotonicTime after_ttl = now + std::chrono::hours(25);
  result = cache.lookup("test_key", after_ttl);
  EXPECT_FALSE(result.cache_hit);
  EXPECT_EQ(result.module, nullptr);
}

TEST_F(ModuleCacheTest, MultipleEntries) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.update("key1", "data1", now);
  cache.update("key2", "data2", now);
  cache.update("key3", "data3", now);

  EXPECT_EQ(cache.size(), 3);

  auto result1 = cache.lookup("key1", now);
  EXPECT_TRUE(result1.cache_hit);
  ASSERT_NE(result1.module, nullptr);
  EXPECT_EQ(*result1.module, "data1");

  auto result2 = cache.lookup("key2", now);
  EXPECT_TRUE(result2.cache_hit);
  ASSERT_NE(result2.module, nullptr);
  EXPECT_EQ(*result2.module, "data2");

  auto result3 = cache.lookup("key3", now);
  EXPECT_TRUE(result3.cache_hit);
  ASSERT_NE(result3.module, nullptr);
  EXPECT_EQ(*result3.module, "data3");
}

TEST_F(ModuleCacheTest, Clear) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.update("key1", "data1", now);
  cache.update("key2", "data2", now);
  EXPECT_EQ(cache.size(), 2);

  cache.clear();
  EXPECT_EQ(cache.size(), 0);

  auto result = cache.lookup("key1", now);
  EXPECT_FALSE(result.cache_hit);
}

TEST_F(ModuleCacheTest, GlobalCacheAccessor) {
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  auto& cache1 = getModuleCache();
  cache1.update("global_key", "global_data", now);

  auto& cache2 = getModuleCache();
  auto result = cache2.lookup("global_key", now);
  EXPECT_TRUE(result.cache_hit);
  ASSERT_NE(result.module, nullptr);
  EXPECT_EQ(*result.module, "global_data");

  // Both should be the same instance.
  EXPECT_EQ(&cache1, &cache2);
}

TEST_F(ModuleCacheTest, InProgressExpiresAfterTimeout) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.markInProgress("test_key", now);

  // Within the 5-minute timeout, in-progress entries should still be present.
  MonotonicTime within_timeout = now + std::chrono::seconds(299);
  auto result = cache.lookup("test_key", within_timeout);
  EXPECT_TRUE(result.cache_hit);
  EXPECT_TRUE(result.fetch_in_progress);

  // After the 5-minute timeout, in-progress entries should be evicted.
  MonotonicTime after_timeout =
      now + std::chrono::seconds(DynamicModuleCache::IN_PROGRESS_TIMEOUT_SECONDS + 1);
  result = cache.lookup("test_key", after_timeout);
  EXPECT_FALSE(result.cache_hit);
  EXPECT_FALSE(result.fetch_in_progress);
  EXPECT_EQ(result.module, nullptr);
}

TEST_F(ModuleCacheTest, UpdateClearsInProgress) {
  DynamicModuleCache cache;
  MonotonicTime now = MonotonicTime(std::chrono::seconds(1000));

  cache.markInProgress("test_key", now);

  auto result = cache.lookup("test_key", now);
  EXPECT_TRUE(result.fetch_in_progress);

  cache.update("test_key", "module_data", now);

  result = cache.lookup("test_key", now);
  EXPECT_FALSE(result.fetch_in_progress);
  ASSERT_NE(result.module, nullptr);
  EXPECT_EQ(*result.module, "module_data");
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
