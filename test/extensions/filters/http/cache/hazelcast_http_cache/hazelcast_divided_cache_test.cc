#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Tests for DIVIDED cache mode.
 */
class HazelcastDividedCacheTest : public HazelcastHttpCacheTestBase {
protected:
  void SetUp() override {
    HazelcastHttpCacheConfig config = HazelcastTestUtil::getTestConfig(false);
    // To test the cache with a real Hazelcast instance, remote cache
    // must be used during tests.
    // cache_ = std::make_unique<HazelcastTestableRemoteCache>(config);
    cache_ = std::make_unique<HazelcastTestableLocalCache>(config);
    cache_->restoreTestConnection();
    cache_->clearTestMaps();
  }
};

TEST_F(HazelcastDividedCacheTest, AbortDividedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr insert_context = cache_->base().makeInsertContext(lookup(RequestPath));
  insert_context->insertHeaders(getResponseHeaders(), false);
  bool ready_for_next = true;
  while (ready_for_next) {
    insert_context->insertBody(
        Buffer::OwnedImpl(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h')),
        [&](bool ready) { ready_for_next = ready; }, false);
  }

  EXPECT_EQ(((HazelcastTestUtil::TEST_MAX_BODY_SIZE + HazelcastTestUtil::TEST_PARTITION_SIZE - 1) /
             HazelcastTestUtil::TEST_PARTITION_SIZE),
            cache_->bodyTestMapSize());
  EXPECT_TRUE(expectLookupSuccessWithFullBody(
      lookup(RequestPath).get(), std::string(HazelcastTestUtil::TEST_MAX_BODY_SIZE, 'h')));
}

TEST_F(HazelcastDividedCacheTest, PreventOverridingCacheEntries) {
  const std::string RequestPath("/prevent/override/cached/response");

  LookupContextPtr lookup_context = lookup(RequestPath);
  const std::string OriginalBody(HazelcastTestUtil::TEST_PARTITION_SIZE * 2, 'h');
  insert(move(lookup_context), getResponseHeaders(), OriginalBody);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  // A possible call to insertion below (using a success lookup context) would be the filter's
  // fault, not an expected behavior.
  const std::string OverriddenBody(HazelcastTestUtil::TEST_PARTITION_SIZE * 3, 'z');
  insert(move(lookup_context), getResponseHeaders(), OverriddenBody);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), OriginalBody));
  EXPECT_EQ(2, cache_->bodyTestMapSize());
  EXPECT_EQ(1, cache_->headerTestMapSize());
}

TEST_F(HazelcastDividedCacheTest, AbortInsertionIfKeyIsLocked) {
  const std::string RequestPath("/only/one/must/insert");

  LookupContextPtr lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  // The first missed lookup must be allowed to make insertion.
  ASSERT(!static_cast<HazelcastLookupContextBase&>(*lookup_context1).isAborted());

  // Following ones must abort the insertion.
  LookupContextPtr lookup_context2;
  std::thread t1([&] {
    // If the second lookup would not be performed in a separate thread, it will acquire
    // the lock even if it's already locked. This is because the key locks on Hazelcast
    // IMap are re-entrant. A locked key can be acquired by the same thread again and
    // again based on its pid.
    lookup_context2 = lookup(RequestPath);
  });
  t1.join();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  ASSERT(static_cast<HazelcastLookupContextBase&>(*lookup_context2).isAborted());

  const std::string Body("hazelcast");
  // second context should not insert even if arrives before the first one.
  insert(move(lookup_context2), getResponseHeaders(), Body);
  lookup_context2 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // first one must do the insertion.
  insert(move(lookup_context1), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), Body));
}

TEST_F(HazelcastDividedCacheTest, MissLookupOnVersionMismatch) {
  const std::string RequestPath1("/miss/on/version/mismatch");

  LookupContextPtr lookup_context = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_hash_key =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantHashKey();

  const std::string Body(HazelcastTestUtil::TEST_PARTITION_SIZE * 2, 'h');
  insert(move(lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath1).get(), Body));

  // Change version of the second partition.
  auto body2 = cache_->base().getBody(variant_hash_key, 1);
  EXPECT_NE(body2, nullptr);
  body2->version(body2->version() + 1);
  cache_->base().putBody(variant_hash_key, 1, *body2);

  // Change happened in the second partition. Lookup to the first one should be successful.
  lookup_context = lookup(RequestPath1);
  std::string partition1 = getBody(*lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE);
  EXPECT_EQ(partition1, std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'));

  std::string fullBody = getBody(*lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE * 2);
  EXPECT_EQ(fullBody, HazelcastTestUtil::abortedBodyResponse());

  // Clean up must be performed for malformed entries.
  EXPECT_EQ(0, cache_->bodyTestMapSize());
  EXPECT_EQ(0, cache_->headerTestMapSize());
}

TEST_F(HazelcastDividedCacheTest, MissDividedLookupOnDifferentKey) {

  const std::string RequestPath("/miss/on/different/key");

  LookupContextPtr lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_hash_key =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantHashKey();

  const std::string Body("hazelcast");
  insert(move(lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), Body));

  // Manipulate the cache entry directly. Cache is not aware of that.
  // The cached key will not be the same with the created one by filter.
  auto header = cache_->base().getHeader(variant_hash_key);
  Key modified = header->variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  header->variantKey(std::move(modified));
  cache_->base().putHeader(variant_hash_key, *header);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, cache_->headerTestMapSize());

  auto modified_header = cache_->base().getHeader(variant_hash_key);
  EXPECT_EQ(2, modified_header->variantKey().custom_fields_size());
}

TEST_F(HazelcastDividedCacheTest, CleanUpCachedResponseOnMissingBody) {
  const std::string RequestPath1("/clean/up/on/missing/body");
  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  uint64_t variant_hash_key =
      static_cast<HazelcastLookupContextBase&>(*lookup_context1).variantHashKey();

  const std::string Body = std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h') +
                           std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'z') +
                           std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'c');

  insert(move(lookup_context1), getResponseHeaders(), Body);
  lookup_context1 = lookup(RequestPath1);

  // Response is cached with the following pattern:
  // variant_hash_key -> HeaderEntry (in header map)
  // variant_hash_key "0" -> Body1 (in body map)
  // variant_hash_key "1" -> Body2 (in body map)
  // variant_hash_key "2" -> Body3 (in body map)
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));

  cache_->removeTestBody(variant_hash_key, 1); // evict Body2.

  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  // Lookup for Body1 is OK.
  lookup_context1->getBody({0, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
                           [](Buffer::InstancePtr&& data) { EXPECT_NE(data, nullptr); });

  // Lookup for Body2 must fail and trigger clean up.
  lookup_context1->getBody(
      {HazelcastTestUtil::TEST_PARTITION_SIZE, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
      [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // On lookup miss, lock is being acquired. It must be released
  // explicitly or let context do the insertion and then release.
  // If not released, the second run for the test fails. Since no
  // insertion follows the missed lookup here, the lock is explicitly
  // released.
  cache_->base().unlock(variant_hash_key);

  // Assert clean up
  EXPECT_EQ(0, cache_->bodyTestMapSize());
  EXPECT_EQ(0, cache_->headerTestMapSize());
}

TEST_F(HazelcastDividedCacheTest, NotCreateBodyOnHeaderOnlyResponse) {
  auto headerOnlyTest = [this](std::string path, bool empty_body) {
    LookupContextPtr lookup_context = lookup(path);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
    insert(move(lookup_context), getResponseHeaders(), empty_body ? "" : nullptr);
    lookup_context = lookup(path);
    EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
    EXPECT_EQ(0, lookup_result_.content_length_);
  };

  // This will pass end_stream = true during header insertion.
  headerOnlyTest("/header/only/response", false);

  // This will pass end_stream = false during header insertion,
  // then empty body for body insertion.
  headerOnlyTest("/empty/body/response", true);

  EXPECT_EQ(0, cache_->bodyTestMapSize());
  EXPECT_EQ(2, cache_->headerTestMapSize());
}

TEST_F(HazelcastDividedCacheTest, AbortDividedOperationsWhenOffline) {
  {
    const std::string RequestPath("/connection/lost/after/insertion");
    LookupContextPtr lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    const std::string Body("s", HazelcastTestUtil::TEST_PARTITION_SIZE);
    insert(move(lookup_context), getResponseHeaders(), Body);
    lookup_context = lookup(RequestPath);
    EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));

    cache_->dropTestConnection();

    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
    insert(move(lookup_context), getResponseHeaders(), Body);

    cache_->restoreTestConnection();

    lookup_context = lookup(RequestPath);
    EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));
  }

  {
    const std::string RequestPath("/connection/lost/during/insertion");
    InsertContextPtr insert_context = cache_->base().makeInsertContext(lookup(RequestPath));
    insert_context->insertHeaders(getResponseHeaders(), false);
    auto insert = [&insert_context](std::string body, bool end_stream) {
      insert_context->insertBody(
          Buffer::OwnedImpl(body), [](bool) {}, end_stream);
    };

    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'), false);
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'z'), false);

    cache_->dropTestConnection();

    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'c'), false);
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 's'), true);

    LookupContextPtr lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    cache_->restoreTestConnection();

    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  }
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
