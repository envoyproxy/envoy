#include "envoy/registry/registry.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/test_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Tests for UNIFIED cache mode.
 */
class HazelcastUnifiedCacheTest : public HazelcastHttpCacheTestBase {
  void SetUp() override {
    HazelcastHttpCacheConfig config = HazelcastTestUtil::getTestConfig(true);
    // To test the cache with a real Hazelcast instance, use remote test cache.
    // cache_ = std::make_unique<RemoteTestCache>(config);
    cache_ = std::make_unique<LocalTestCache>(config);
    cache_->start();
    cache_->getTestAccessor().clearMaps();
  }
};

TEST_F(HazelcastUnifiedCacheTest, AbortUnifiedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr insert_context = cache_->makeInsertContext(lookup(RequestPath));
  insert_context->insertHeaders(getResponseHeaders(), false);
  bool ready_for_next = true;
  while (ready_for_next) {
    insert_context->insertBody(
        Buffer::OwnedImpl(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE / 3, 'h')),
        [&](bool ready) { ready_for_next = ready; }, false);
  }

  EXPECT_TRUE(expectLookupSuccessWithFullBody(
      lookup(RequestPath).get(), std::string(HazelcastTestUtil::TEST_MAX_BODY_SIZE, 'h')));
}

TEST_F(HazelcastUnifiedCacheTest, AllowOverrideExistingResponse) {
  const std::string RequestPath("/allow/override/unified/response");

  LookupContextPtr lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context2 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast-first");
  const std::string Body2("hazelcast-second");

  insert(move(lookup_context1), getResponseHeaders(), Body1);
  lookup_context1 = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body1));

  insert(move(lookup_context2), getResponseHeaders(), Body2);
  lookup_context2 = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context2.get(), Body2));
}

TEST_F(HazelcastUnifiedCacheTest, UnifiedHeaderOnlyResponse) {
  InsertContextPtr insert_context = cache_->makeInsertContext(lookup("/header/only"));
  insert_context->insertHeaders(getResponseHeaders(), true);
  LookupContextPtr lookup_context = lookup("/header/only");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  EXPECT_EQ(0, lookup_result_.content_length_);
}

TEST_F(HazelcastUnifiedCacheTest, MissUnifiedLookupOnDifferentKey) {

  const std::string RequestPath("/miss/on/different/key");

  LookupContextPtr lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_hash_key =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantHashKey();

  const std::string Body("hazelcast");
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));

  // Manipulate the cache entry directly. Cache is not aware of that.
  // The cached key will not be the same with the created one by filter.
  auto response = cache_->getResponse(variant_hash_key);
  Key modified = response->header().variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  response->header().variantKey(std::move(modified));
  cache_->getTestAccessor().insertResponse(cache_->mapKey(variant_hash_key), *response);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, cache_->getTestAccessor().responseMapSize());
}

TEST_F(HazelcastUnifiedCacheTest, AbortUnifiedOperationsWhenOffline) {
  const std::string RequestPath1("/online/");
  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body(HazelcastTestUtil::TEST_PARTITION_SIZE, 's');
  insert(move(lookup_context1), getResponseHeaders(), Body);
  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));

  // These lookup are not marked as to be aborted since connection is still alive.
  // They will be used to test insertion behavior when cache is offline.
  LookupContextPtr succeed_lookup1 = lookup(RequestPath1);
  LookupContextPtr succeed_lookup2 = lookup(RequestPath1);
  LookupContextPtr succeed_lookup3 = lookup(RequestPath1);

  cache_->getTestAccessor().dropConnection();

  // UnifiedLookupContext::getHeaders when HazelcastClientOfflineException is thrown.
  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  // lookup has marked insertion to be aborted. Hence the following should do no-op.
  insert(move(lookup_context1), getResponseHeaders(), Body);

  // UnifiedLookupContext::getHeaders when OperationTimeoutException is thrown.
  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // UnifiedLookupContext::getHeaders when std::exception is thrown.
  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // UnifiedInsertContext::insertResponse when HazelcastClientOfflineException is thrown.
  insert(move(succeed_lookup1), getResponseHeaders(), Body);
  // UnifiedInsertContext::insertResponse when OperationTimeoutException is thrown.
  insert(move(succeed_lookup2), getResponseHeaders(), Body);
  // UnifiedInsertContext::insertResponse when std::exception is thrown.
  insert(move(succeed_lookup3), getResponseHeaders(), Body);

  cache_->getTestAccessor().restoreConnection();

  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));
}

TEST_F(HazelcastUnifiedCacheTest, CoverRemoteOperations) {
  // Since not a real Hazelcast instance is used during tests,
  // in order to keep the coverage in a desired level this test
  // covers the calls made to Hazelcast cluster. Otherwise the coverage
  // stays at 90% without including the remote calls.
  ClientConfig config;
  config.getNetworkConfig().setConnectionAttemptLimit(100);
  config.getNetworkConfig().setConnectionAttemptPeriod(10000);
  config.getConnectionStrategyConfig().setAsyncStart(true);
  std::unique_ptr<HazelcastClient> client = std::make_unique<HazelcastClient>(config);
  client->shutdown();
  HazelcastClusterAccessor accessor(*cache_, ClientConfig(), "coverage", 10);
  EXPECT_FALSE(accessor.isRunning());
  accessor.setClient(std::move(client));
  using exception = hazelcast::client::exception::HazelcastClientOfflineException;
  EXPECT_FALSE(accessor.isRunning());
  EXPECT_STREQ("dev", accessor.clusterName().c_str());
  EXPECT_STREQ("coverage:10-div", accessor.headerMapName().c_str());
  EXPECT_STREQ("coverage-uni", accessor.responseMapName().c_str());
  EXPECT_THROW(accessor.putHeader(1, HazelcastHeaderEntry()), exception);
  EXPECT_THROW(accessor.putBody("1", HazelcastBodyEntry()), exception);
  EXPECT_THROW(accessor.putResponse(1, HazelcastResponseEntry()), exception);
  EXPECT_THROW(accessor.getHeader(1), exception);
  EXPECT_THROW(accessor.getBody("1"), exception);
  EXPECT_THROW(accessor.getResponse(1), exception);
  EXPECT_THROW(accessor.removeBodyAsync("1"), exception);
  EXPECT_THROW(accessor.removeHeader(1), exception);
  EXPECT_THROW(accessor.tryLock(1, true), exception);
  EXPECT_THROW(accessor.unlock(1, true), exception);
  EXPECT_THROW(accessor.unlock(1, false), exception);
  accessor.disconnect();
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
