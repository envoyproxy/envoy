#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/test_util.h"

#include "gmock/gmock.h"

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
    // To test the cache with a real Hazelcast instance, use remote test cache.
    // cache_ = std::make_unique<HazelcastRemoteTestCache>(config);
    cache_ = std::make_unique<HazelcastLocalTestCache>(config);
    cache_->start();
    cache_->getTestAccessor().clearMaps();
  }
};

template <typename K, typename V> class MockEntryEvictedEvent : public EntryEvent<K, V> {
public:
  MockEntryEvictedEvent(const Member& member)
      : EntryEvent<K, V>("MockEntryEvent", member, EntryEventType::EVICTED) {}
  MOCK_METHOD(const K*, getKeyObject, (), (const));
  MOCK_METHOD(const V*, getOldValueObject, (), (const));
};

TEST_F(HazelcastDividedCacheTest, AbortDividedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr insert_context = cache_->makeInsertContext(lookup(RequestPath));
  insert_context->insertHeaders(getResponseHeaders(), false);
  bool ready_for_next = true;
  while (ready_for_next) {
    insert_context->insertBody(
        Buffer::OwnedImpl(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h')),
        [&](bool ready) { ready_for_next = ready; }, false);
  }

  EXPECT_EQ(((HazelcastTestUtil::TEST_MAX_BODY_SIZE + HazelcastTestUtil::TEST_PARTITION_SIZE - 1) /
             HazelcastTestUtil::TEST_PARTITION_SIZE),
            cache_->getTestAccessor().bodyMapSize());
  EXPECT_TRUE(expectLookupSuccessWithFullBody(
      lookup(RequestPath).get(), std::string(HazelcastTestUtil::TEST_MAX_BODY_SIZE, 'h')));
}

TEST_F(HazelcastDividedCacheTest, AllowOverridingCacheEntries) {
  const std::string RequestPath("/allow/override/cached/response");

  LookupContextPtr lookup_context = lookup(RequestPath);
  const std::string OriginalBody(HazelcastTestUtil::TEST_PARTITION_SIZE * 3, 'h');
  insert(move(lookup_context), getResponseHeaders(), OriginalBody);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  const std::string OverriddenBody(HazelcastTestUtil::TEST_PARTITION_SIZE * 2, 'z');
  insert(move(lookup_context), getResponseHeaders(), OverriddenBody);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), OverriddenBody));
}

TEST_F(HazelcastDividedCacheTest, CleanBodyOnHeaderEviction) {
  LookupContextPtr lookup_context = lookup("/header/eviction/");
  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantKeyHash();
  const int BodyCount = 3;
  insert(move(lookup_context), getResponseHeaders(),
         std::string(HazelcastTestUtil::TEST_PARTITION_SIZE * BodyCount, 'h'));
  EXPECT_EQ(1, cache_->getTestAccessor().headerMapSize());
  EXPECT_EQ(BodyCount, cache_->getTestAccessor().bodyMapSize());

  auto keyObject = std::make_unique<int64_t>(mapKey(variant_key_hash));
  auto valueObject = cache_->getHeader(variant_key_hash);
  EXPECT_NE(nullptr, valueObject);

  Member m;
  MockEntryEvictedEvent<int64_t, HazelcastHeaderEntry> mock_event(m);
  EXPECT_CALL(mock_event, getOldValueObject()).WillRepeatedly(testing::Return(valueObject.get()));
  EXPECT_CALL(mock_event, getKeyObject()).WillRepeatedly(testing::Return(keyObject.get()));

  EXPECT_EQ(BodyCount, cache_->getTestAccessor().bodyMapSize());

  HeaderMapEntryListener listener(*cache_);
  listener.entryAdded(mock_event);   // no-op
  listener.entryRemoved(mock_event); // no-op
  listener.entryUpdated(mock_event); // no-op
  listener.entryExpired(mock_event); // no-op
  listener.entryMerged(mock_event);  // no-op

  MapEvent null_event(m, EntryEventType::UNDEFINED, "null_event", 0);
  listener.mapEvicted(null_event); // no-op
  listener.mapCleared(null_event); // no-op

  EXPECT_EQ(BodyCount, cache_->getTestAccessor().bodyMapSize());
  listener.entryEvicted(mock_event);
  EXPECT_EQ(0, cache_->getTestAccessor().bodyMapSize());
}

TEST_F(HazelcastDividedCacheTest, AbortInsertionIfKeyIsLocked) {
  const std::string RequestPath("/only/one/must/insert");

  LookupContextPtr lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // Insertion starts for lookup_context1.
  InsertContextPtr insert_context = cache_->makeInsertContext(std::move(lookup_context1));
  insert_context->insertHeaders(getResponseHeaders(), false);
  auto context1_insert = [&insert_context](std::string body, bool end_stream) {
    insert_context->insertBody(
        Buffer::OwnedImpl(body), [](bool) {}, end_stream);
  };
  context1_insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'), false);
  context1_insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'), false);
  // Insertion has not finished for lookup_context1 yet.

  std::thread t1([&] {
    // If the second lookup and insertion would not be performed in a separate thread,
    // it will acquire the lock even if it's already locked. This is because the key
    // locks on Hazelcast IMap are re-entrant. A locked key can be acquired by the same
    // thread again and again based on its pid. Using a different thread here simulates
    // another request that yields cache insertion.
    LookupContextPtr lookup_context2 = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
    insert(move(lookup_context2), getResponseHeaders(), "ignored");
  });
  t1.join();

  context1_insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'), true);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(
      lookup(RequestPath).get(), std::string(HazelcastTestUtil::TEST_PARTITION_SIZE * 3, 'h')));
}

TEST_F(HazelcastDividedCacheTest, MissLookupOnVersionMismatch) {
  const std::string RequestPath1("/miss/on/version/mismatch");

  LookupContextPtr lookup_context = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantKeyHash();

  const std::string Body(HazelcastTestUtil::TEST_PARTITION_SIZE * 2, 'h');
  insert(move(lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath1).get(), Body));

  // Change version of the second partition.
  auto body2 = cache_->getBody(variant_key_hash, 1);
  EXPECT_NE(body2, nullptr);
  body2->version(body2->version() + 1);
  cache_->putBody(variant_key_hash, 1, *body2);

  // Change happened in the second partition. Lookup to the first one should be successful.
  lookup_context = lookup(RequestPath1);
  std::string partition1 = getBody(*lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE);
  EXPECT_EQ(partition1, std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'));

  std::string fullBody = getBody(*lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE * 2);
  EXPECT_EQ(fullBody, HazelcastTestUtil::abortedBodyResponse());

  // Clean up must be performed for malformed entries.
  EXPECT_EQ(0, cache_->getTestAccessor().bodyMapSize());
  EXPECT_EQ(0, cache_->getTestAccessor().headerMapSize());
}

TEST_F(HazelcastDividedCacheTest, MissDividedLookupOnDifferentKey) {

  const std::string RequestPath("/miss/on/different/key");

  LookupContextPtr lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup_context).variantKeyHash();

  const std::string Body("hazelcast");
  insert(move(lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath).get(), Body));

  // Manipulate the cache entry directly. Cache is not aware of that.
  // The cached key will not be the same with the created one by filter.
  auto header = cache_->getHeader(variant_key_hash);
  Key modified = header->variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  header->variantKey(std::move(modified));
  cache_->putHeader(variant_key_hash, *header);

  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, cache_->getTestAccessor().headerMapSize());

  auto modified_header = cache_->getHeader(variant_key_hash);
  EXPECT_EQ(*header, *modified_header);
}

TEST_F(HazelcastDividedCacheTest, CleanUpCachedResponseOnMissingBody) {
  const std::string RequestPath1("/clean/up/on/missing/body");
  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup_context1).variantKeyHash();

  const std::string Body = std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h') +
                           std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'z') +
                           std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'c');

  insert(move(lookup_context1), getResponseHeaders(), Body);
  lookup_context1 = lookup(RequestPath1);

  // Response is cached with the following pattern:
  // variant_key_hash -> HeaderEntry (in header map)
  // variant_key_hash "0" -> Body1 (in body map)
  // variant_key_hash "1" -> Body2 (in body map)
  // variant_key_hash "2" -> Body3 (in body map)
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context1.get(), Body));

  cache_->getTestAccessor().removeBody(orderedMapKey(variant_key_hash, 1)); // evict Body2.

  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  // Lookup for Body1 is OK.
  lookup_context1->getBody({0, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
                           [](Buffer::InstancePtr&& data) { EXPECT_NE(data, nullptr); });

  {
    std::thread t1([&] {
      // If another thread locks the key, then the current one should not perform
      // clean up. The lock here will serve the purpose.
      EXPECT_TRUE(cache_->tryLock(variant_key_hash));
    });
    t1.join();

    // Lookup for Body2 must fail and trigger clean up. But due to locked key (might
    // be acquired by another clean up process for this entry), clean up must do no-op.
    lookup_context1->getBody(
        {HazelcastTestUtil::TEST_PARTITION_SIZE, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
        [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

    EXPECT_NE(0, cache_->getTestAccessor().bodyMapSize()); // clean up is not performed.

    cache_->unlock(variant_key_hash);
  }

  {
    // Clean up must be aborted when header versions are mismatched.
    // This prevents clean up operation for wrong entries.
    auto header = cache_->getHeader(variant_key_hash);
    int32_t original_version = header->version();
    header->version(original_version - 1);
    cache_->putHeader(variant_key_hash, *header);

    lookup_context1->getBody(
        {HazelcastTestUtil::TEST_PARTITION_SIZE, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
        [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

    EXPECT_NE(0, cache_->getTestAccessor().bodyMapSize());

    header->version(original_version);
    cache_->putHeader(variant_key_hash, *header);
  }

  {
    // Clean up must be performed after body miss.
    lookup_context1->getBody(
        {HazelcastTestUtil::TEST_PARTITION_SIZE, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
        [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

    lookup_context1 = lookup(RequestPath1);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    // Assert clean up
    EXPECT_EQ(0, cache_->getTestAccessor().bodyMapSize());
    EXPECT_EQ(0, cache_->getTestAccessor().headerMapSize());
  }

  // Cache must handle the connection failure during clean up.
  cache_->getTestAccessor().failOnLock();
  cache_->onMissingBody(0, 0, 0); // HazelcastClientOfflineException
  cache_->onMissingBody(0, 0, 0); // std::exception
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

  EXPECT_EQ(0, cache_->getTestAccessor().bodyMapSize());
  EXPECT_EQ(2, cache_->getTestAccessor().headerMapSize());
}

TEST_F(HazelcastDividedCacheTest, AbortDividedOperationsWhenOffline) {
  // Operations are arranged to test all exception using Local Test Accessor.
  // Changing the order might not cause test to fail but to uncover some exceptions.
  {
    const std::string RequestPath("/connection/lost/after/insertion");
    LookupContextPtr lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    const std::string Body(HazelcastTestUtil::TEST_PARTITION_SIZE, 's');
    insert(move(lookup_context), getResponseHeaders(), Body);
    lookup_context = lookup(RequestPath);
    EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));

    cache_->getTestAccessor().dropConnection();

    // std::exception case.
    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    // HazelcastClientOfflineException case.
    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    // OperationTimeoutException case.
    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    insert(move(lookup_context), getResponseHeaders(), Body);

    cache_->getTestAccessor().restoreConnection();

    lookup_context = lookup(RequestPath);
    EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));
  }

  {
    const std::string RequestPath("/connection/lost/during/insertion");
    InsertContextPtr insert_context = cache_->makeInsertContext(lookup(RequestPath));
    insert_context->insertHeaders(getResponseHeaders(), false);
    auto insert = [&insert_context](std::string body, bool end_stream) {
      insert_context->insertBody(
          Buffer::OwnedImpl(body), [](bool) {}, end_stream);
    };

    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'), false);
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'z'), false);

    cache_->getTestAccessor().dropConnection();

    // testing std::exception case.
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'c'), true);
    // testing HazelcastClientOfflineException case.
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 's'), true);
    // testing OperationTimeoutException case.
    insert(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 't'), true);

    LookupContextPtr lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

    cache_->getTestAccessor().restoreConnection();

    lookup_context = lookup(RequestPath);
    EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  }
}

TEST_F(HazelcastDividedCacheTest, FailDuringLock) {
  // Tests the case when header lookup is performed without exception but tryLock for
  // the insertion permission is failed. Operations are arranged to test all exception
  // using Local Test Accessor. Changing the order might not cause test to fail but
  // to uncover some exceptions.
  const std::string RequestPath("/failed/during/try/lock");
  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*lookup(RequestPath)).variantKeyHash();

  std::thread t1([&] {
    // To make this test compatible with HazelcastRemoteTestCache, the key is locked here
    // explicitly. This behavior will cause tryLock to return false for further
    // trials. Hence the lookups below will throw exception for local cache and
    // return false for remote cache. This has no effect on local test but lack of
    // this locking causes remote test to fail. Notice that if this locking would not
    // be performed by a different thread, following insertions in this thread would
    // be able to lock the key again and again for HazelcastRemoteTestCache.
    EXPECT_TRUE(cache_->tryLock(variant_key_hash));
  });
  t1.join();

  // This will cause LocalTestAccessor::tryLock to raise error.
  cache_->getTestAccessor().failOnLock();

  insert(lookup(RequestPath), getResponseHeaders(), "aborted"); // std::exception
  insert(lookup(RequestPath), getResponseHeaders(), "aborted"); // HazelcastClientOfflineException
  insert(lookup(RequestPath), getResponseHeaders(), "aborted"); // OperationTimeoutException
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  cache_->unlock(variant_key_hash);
}

TEST_F(HazelcastDividedCacheTest, FailDuringBodyLookupWhenHeaderSucceeds) {
  // Tests the case when header lookup succeeds but body lookup fails.
  const int body_size = HazelcastTestUtil::TEST_PARTITION_SIZE * 2;
  const std::string RequestPath("/fail/on/body");
  const std::string Body(body_size, 'h');

  LookupContextPtr lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);
  // lookup for header is OK.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  // Lookup for Body1 is OK.
  lookup_context->getBody({0, HazelcastTestUtil::TEST_PARTITION_SIZE},
                          [](Buffer::InstancePtr&& data) { EXPECT_NE(data, nullptr); });

  cache_->getTestAccessor().dropConnection();

  // Lookup for body should be aborted on HazelcastOffline exception.
  lookup_context->getBody({0, body_size},
                          [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

  // Lookup for body should be aborted on TimeOut exception.
  lookup_context->getBody({0, body_size},
                          [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

  // Lookup for body should be aborted on std::exception.
  lookup_context->getBody({0, body_size},
                          [](Buffer::InstancePtr&& data) { EXPECT_EQ(data, nullptr); });

  cache_->getTestAccessor().restoreConnection();

  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup_context.get(), Body));
}

TEST_F(HazelcastDividedCacheTest, AbortInsertionWhenLockLeftover) {
  // Happens when a lookup context acquires the lock for insertion but fails to
  // unlock before insertion. In such a case, the fail over must be done by
  // max.leaseTime of locks set on the Hazelcast server.
  const std::string RequestPath1("/connection/lost/on/lock/acquisition/1");
  const std::string RequestPath2("/connection/lost/on/lock/acquisition/2");
  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  InsertContextPtr insert_context1 = cache_->makeInsertContext(std::move(lookup_context1));
  InsertContextPtr insert_context2 = cache_->makeInsertContext(std::move(lookup_context2));

  cache_->getTestAccessor().dropConnection();
  insert_context1->insertHeaders(getResponseHeaders(), true);
  insert_context2->insertHeaders(getResponseHeaders(), true);
  cache_->getTestAccessor().restoreConnection();

  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  insert(move(lookup_context1), getResponseHeaders(), "");
  // insertion fails since the key is still locked.
  lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  insert(move(lookup_context2), getResponseHeaders(), "");
  // insertion fails since the key is still locked.
  lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
