#include "envoy/registry/registry.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Tests for UNIFIED cache mode.
 */
class HazelcastUnifiedCacheTest : public HazelcastHttpCacheTestBase {
  void SetUp() {
    HazelcastHttpCacheConfig config = HazelcastTestUtil::getTestConfig(true);
    // To test the cache with a real Hazelcast instance, remote cache
    // must be used during tests.
    // cache_ = std::make_unique<HazelcastTestableRemoteCache>(config);
    cache_ = std::make_unique<HazelcastTestableLocalCache>(config);
    cache_->restoreTestConnection();
    cache_->clearTestMaps();
  }
};

TEST_F(HazelcastUnifiedCacheTest, AbortUnifiedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr insert_context = cache_->base().makeInsertContext(lookup(RequestPath));
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

TEST_F(HazelcastUnifiedCacheTest, PutResponseOnlyWhenAbsent) {
  const std::string RequestPath("/only/one/must/insert");

  LookupContextPtr lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  LookupContextPtr lookup_context2 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast");
  const std::string Body2("hazelcast.distributed.caching");

  // The second context should insert if the cache is empty for this request.
  insert(move(lookup_context1), getResponseHeaders(), Body1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), Body1));

  // The first context should not do the insertion/override the existing value.
  insert(move(lookup_context2), getResponseHeaders(), Body2);
  // Response body must remain as Body1
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), Body1));
}

TEST_F(HazelcastUnifiedCacheTest, DoNotOverrideExistingResponse) {
  const std::string RequestPath1("/on/unified/not/override");

  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context2 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast-first");
  const std::string Body2("hazelcast-second");

  insert(move(lookup_context1), getResponseHeaders(), Body1);
  insert(move(lookup_context2), getResponseHeaders(), Body2);

  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body1));
}

TEST_F(HazelcastUnifiedCacheTest, UnifiedHeaderOnlyResponse) {
  InsertContextPtr insert_context = cache_->base().makeInsertContext(lookup("/header/only"));
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
  auto response = cache_->base().getResponse(variant_hash_key);
  Key modified = response->header().variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  response->header().variantKey(std::move(modified));
  cache_->putTestResponse(variant_hash_key, *response);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, cache_->responseTestMapSize());
}

TEST_F(HazelcastUnifiedCacheTest, AbortUnifiedOperationsWhenOffline) {
  const std::string RequestPath1("/online/");
  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body("s", HazelcastTestUtil::TEST_PARTITION_SIZE);
  insert(move(lookup_context1), getResponseHeaders(), Body);
  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));

  cache_->dropTestConnection();

  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  insert(move(lookup_context1), getResponseHeaders(), Body);

  cache_->restoreTestConnection();

  lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
