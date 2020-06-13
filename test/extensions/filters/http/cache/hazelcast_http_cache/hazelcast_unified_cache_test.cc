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
    HazelcastHttpCacheConfig typed_config = HazelcastTestUtil::getTestTypedConfig(true);
    envoy::extensions::filters::http::cache::v3alpha::CacheConfig cache_config =
        HazelcastTestUtil::getTestCacheConfig();
    cache_ = std::make_unique<HazelcastHttpCache>(std::move(typed_config), cache_config);
    // To test the cache with a real Hazelcast instance, use remote test accessor.
    // cache_->start(HazelcastTestUtil::getTestRemoteAccessor(*cache_));
    cache_->start(std::make_unique<LocalTestAccessor>());
    getTestAccessor().clearMaps();
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

  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantKeyHash();

  const std::string Body("hazelcast");
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));

  // Manipulate the cache entry directly. Cache is not aware of that.
  // The cached key will not be the same with the created one by filter.
  auto response = cache_->getResponse(variant_key_hash);
  Key modified = response->header().variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  response->header().variantKey(std::move(modified));
  getTestAccessor().insertResponse(mapKey(variant_key_hash), *response);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented in this case.
  InsertContextPtr insert_context = cache_->makeInsertContext(std::move(lookup_context));
  insert_context->insertHeaders(getResponseHeaders(), false);
  insert_context->insertBody(
      Buffer::OwnedImpl(Body), [](bool ready) { EXPECT_FALSE(ready); }, true);
  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, getTestAccessor().responseMapSize());
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

  getTestAccessor().dropConnection();

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

  getTestAccessor().restoreConnection();

  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));
}

TEST_F(HazelcastUnifiedCacheTest, CoverRemoteOperations) {
  // Since not a real Hazelcast instance is used during tests,
  // in order to keep the coverage in a desired level this test
  // covers the calls made to Hazelcast cluster. Otherwise the coverage
  // stays at 90% without including the remote calls.
  HazelcastClusterAccessor accessor(*cache_, ClientConfig(), "coverage", 10);
  const std::string info = accessor.startInfo();
  EXPECT_NE(info.find("profile: UNIFIED"), std::string::npos);
  EXPECT_NE(info.find(absl::StrFormat("Max body size: %d", cache_->maxBodyBytes())),
            std::string::npos);
  EXPECT_FALSE(accessor.isRunning());
  EXPECT_STREQ("", accessor.clusterName().c_str());
  EXPECT_THROW(accessor.putHeader(1, HazelcastHeaderEntry()), EnvoyException);
  EXPECT_THROW(accessor.putBody("1", HazelcastBodyEntry()), EnvoyException);
  EXPECT_THROW(accessor.putResponse(1, HazelcastResponseEntry()), EnvoyException);
  EXPECT_THROW(accessor.getHeader(1), EnvoyException);
  EXPECT_THROW(accessor.getBody("1"), EnvoyException);
  EXPECT_THROW(accessor.getResponse(1), EnvoyException);
  EXPECT_THROW(accessor.removeBodyAsync("1"), EnvoyException);
  EXPECT_THROW(accessor.removeHeader(1), EnvoyException);
  EXPECT_THROW(accessor.tryLock(1, true), EnvoyException);
  EXPECT_THROW(accessor.unlock(1, true), EnvoyException);
  EXPECT_THROW(accessor.unlock(1, false), EnvoyException);
  accessor.disconnect();
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
