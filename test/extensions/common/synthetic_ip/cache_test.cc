#include "source/extensions/common/synthetic_ip/cache.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {
namespace {

class SyntheticIpCacheTest : public testing::Test {
public:
  SyntheticIpCacheTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

// Test basic put and lookup functionality
TEST_F(SyntheticIpCacheTest, BasicPutAndLookup) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Put an entry
  cache.put("100.80.0.5", "app.azure.com", std::chrono::seconds(300));

  // Lookup should succeed
  auto result = cache.lookup("100.80.0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->hostname, "app.azure.com");

  // Contains should return true
  EXPECT_TRUE(cache.contains("100.80.0.5"));

  // Size should be 1
  EXPECT_EQ(cache.size(), 1);
}

// Test that lookups for missing entries return nullopt
TEST_F(SyntheticIpCacheTest, LookupMissing) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Lookup non-existent entry
  auto result = cache.lookup("100.80.0.5");
  EXPECT_FALSE(result.has_value());

  // Contains should return false
  EXPECT_FALSE(cache.contains("100.80.0.5"));
}

// Test that entries expire after TTL
TEST_F(SyntheticIpCacheTest, EntryExpiration) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Put an entry with 5 second TTL
  cache.put("100.80.0.5", "app.azure.com", std::chrono::seconds(5));

  // Should be present initially
  EXPECT_TRUE(cache.contains("100.80.0.5"));

  // Advance time by 3 seconds - should still be present
  time_system_.advanceTimeWait(std::chrono::seconds(3));
  EXPECT_TRUE(cache.contains("100.80.0.5"));

  // Advance time by 3 more seconds (total 6) - should be expired
  time_system_.advanceTimeWait(std::chrono::seconds(3));
  EXPECT_FALSE(cache.contains("100.80.0.5"));

  // Lookup should return nullopt
  auto result = cache.lookup("100.80.0.5");
  EXPECT_FALSE(result.has_value());
}

// Test updating an existing entry
TEST_F(SyntheticIpCacheTest, UpdateEntry) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Put initial entry
  cache.put("100.80.0.5", "app1.azure.com", std::chrono::seconds(300));

  // Verify initial entry
  auto result = cache.lookup("100.80.0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->hostname, "app1.azure.com");

  // Update entry with new hostname
  cache.put("100.80.0.5", "app2.azure.com", std::chrono::seconds(300));

  // Verify updated entry
  result = cache.lookup("100.80.0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->hostname, "app2.azure.com");

  // Size should still be 1
  EXPECT_EQ(cache.size(), 1);
}

// Test removing entries
TEST_F(SyntheticIpCacheTest, RemoveEntry) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Put entries
  cache.put("100.80.0.5", "app1.azure.com", std::chrono::seconds(300));
  cache.put("100.80.0.6", "app2.azure.com", std::chrono::seconds(300));

  EXPECT_EQ(cache.size(), 2);

  // Remove one entry
  cache.remove("100.80.0.5");

  // Removed entry should not be found
  EXPECT_FALSE(cache.contains("100.80.0.5"));

  // Other entry should still exist
  EXPECT_TRUE(cache.contains("100.80.0.6"));

  EXPECT_EQ(cache.size(), 1);
}

// Test clearing the cache
TEST_F(SyntheticIpCacheTest, ClearCache) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 1000);

  // Put multiple entries
  cache.put("100.80.0.5", "app1.azure.com", std::chrono::seconds(300));
  cache.put("100.80.0.6", "app2.azure.com", std::chrono::seconds(300));
  cache.put("100.80.0.7", "app3.azure.com", std::chrono::seconds(300));

  EXPECT_EQ(cache.size(), 3);

  // Clear cache
  cache.clear();

  // All entries should be gone
  EXPECT_EQ(cache.size(), 0);
  EXPECT_FALSE(cache.contains("100.80.0.5"));
  EXPECT_FALSE(cache.contains("100.80.0.6"));
  EXPECT_FALSE(cache.contains("100.80.0.7"));
}

// Test max_entries limit with eviction of oldest
TEST_F(SyntheticIpCacheTest, MaxEntriesEviction) {
  // Create cache with max 3 entries
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 3);

  // Add 3 entries
  cache.put("100.80.0.1", "app1.azure.com", std::chrono::seconds(300));
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  cache.put("100.80.0.2", "app2.azure.com", std::chrono::seconds(300));
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  cache.put("100.80.0.3", "app3.azure.com", std::chrono::seconds(300));

  EXPECT_EQ(cache.size(), 3);

  // Add 4th entry - should evict oldest (100.80.0.1)
  cache.put("100.80.0.4", "app4.azure.com", std::chrono::seconds(300));

  EXPECT_EQ(cache.size(), 3);
  EXPECT_FALSE(cache.contains("100.80.0.1")); // Oldest should be evicted
  EXPECT_TRUE(cache.contains("100.80.0.2"));
  EXPECT_TRUE(cache.contains("100.80.0.3"));
  EXPECT_TRUE(cache.contains("100.80.0.4"));
}

// Test that accessing an entry updates last_accessed time
TEST_F(SyntheticIpCacheTest, LastAccessedUpdated) {
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(60), 3);

  // Add 3 entries
  cache.put("100.80.0.1", "app1.azure.com", std::chrono::seconds(300));
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  cache.put("100.80.0.2", "app2.azure.com", std::chrono::seconds(300));
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  cache.put("100.80.0.3", "app3.azure.com", std::chrono::seconds(300));
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  // Access the oldest entry to update its last_accessed time
  cache.lookup("100.80.0.1");
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));

  // Add 4th entry - should now evict 100.80.0.2 (oldest by access time)
  cache.put("100.80.0.4", "app4.azure.com", std::chrono::seconds(300));

  EXPECT_EQ(cache.size(), 3);
  EXPECT_TRUE(cache.contains("100.80.0.1"));  // Should still be here (was accessed)
  EXPECT_FALSE(cache.contains("100.80.0.2")); // Should be evicted
  EXPECT_TRUE(cache.contains("100.80.0.3"));
  EXPECT_TRUE(cache.contains("100.80.0.4"));
}

// Test periodic eviction of expired entries
TEST_F(SyntheticIpCacheTest, PeriodicEviction) {
  // Create cache with 10 second eviction interval
  SyntheticIpCache cache(*dispatcher_, std::chrono::seconds(10), 1000);

  // Add entries with different TTLs
  cache.put("100.80.0.1", "app1.azure.com", std::chrono::seconds(5));
  cache.put("100.80.0.2", "app2.azure.com", std::chrono::seconds(15));
  cache.put("100.80.0.3", "app3.azure.com", std::chrono::seconds(25));

  EXPECT_EQ(cache.size(), 3);

  // Advance time by 7 seconds and run dispatcher
  time_system_.advanceTimeWait(std::chrono::seconds(7));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // First entry should be expired, but not yet evicted (eviction runs every 10s)
  EXPECT_EQ(cache.size(), 3);
  EXPECT_FALSE(cache.contains("100.80.0.1")); // Expired on access

  // Advance time by 5 more seconds (total 12) and run dispatcher
  // This should trigger periodic eviction
  time_system_.advanceTimeWait(std::chrono::seconds(5));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // After eviction runs, expired entries should be removed
  EXPECT_EQ(cache.size(), 2);
  EXPECT_TRUE(cache.contains("100.80.0.2"));
  EXPECT_TRUE(cache.contains("100.80.0.3"));
}

} // namespace
} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
