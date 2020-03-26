#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"
#include "test/extensions/filters/http/cache/hazelcast_http_cache/util.h"

#include "envoy/registry/registry.h"
#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

class HazelcastDividedCacheTest : public HazelcastHttpCacheTestBase {
protected:
  static void SetUpTestSuite() {
    HazelcastHttpCacheConfig cfg = HazelcastTestUtil::getTestConfig(false);
    hz_cache_ = new HazelcastHttpCache(cfg);
    hz_cache_->connect();
    clearMaps();
  }
};

TEST_F(HazelcastDividedCacheTest, SimpleMissAndPutEntries) {
  const std::string RequestPath1("/size/minus/one");
  const std::string RequestPath2("/exactly/size");
  const std::string RequestPath3("/size/plus/one");

  LookupContextPtr name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr name_lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr name_lookup_context3 = lookup(RequestPath3);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("s", HazelcastTestUtil::TEST_PARTITION_SIZE * 2 - 1);
  const std::string Body2("s", HazelcastTestUtil::TEST_PARTITION_SIZE * 2);
  const std::string Body3("s", HazelcastTestUtil::TEST_PARTITION_SIZE * 2 + 1);

  insert(move(name_lookup_context1), getResponseHeaders(), Body1);
  insert(move(name_lookup_context2), getResponseHeaders(), Body2);
  insert(move(name_lookup_context3), getResponseHeaders(), Body3);

  name_lookup_context1 = lookup(RequestPath1);
  name_lookup_context2 = lookup(RequestPath2);
  name_lookup_context3 = lookup(RequestPath3);

  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body1));
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context2.get(), Body2));
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context3.get(), Body3));

  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, HandleRangedResponses) {
  // make the size even for the sake of divisions below.
  const int size = HazelcastTestUtil::TEST_PARTITION_SIZE & 1 ?
                   HazelcastTestUtil::TEST_PARTITION_SIZE + 1 :
                   HazelcastTestUtil::TEST_PARTITION_SIZE;

  const std::string RequestPath("/ranged/responses");

  LookupContextPtr name_lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body = std::string(size, 'h') +
                           std::string(size, 'z') + std::string(size, 'c');

  insert(move(name_lookup_context1), getResponseHeaders(), Body);
  name_lookup_context1 = lookup(RequestPath);

  EXPECT_EQ(std::string(size, 'h'), getBody(*name_lookup_context1, 0, size));
  EXPECT_EQ(std::string(size, 'z'), getBody(*name_lookup_context1, size, size * 2));
  EXPECT_EQ(std::string(size / 2, 'h') + std::string(size / 2, 'z'),
    getBody(*name_lookup_context1, size / 2, size + size / 2));
  EXPECT_EQ(std::string("h") + std::string(size, 'z') + std::string("c"),
    getBody(*name_lookup_context1, size - 1, 2 * size + 1));
  EXPECT_EQ(Body, getBody(*name_lookup_context1, 0, size * 3));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, AbortDividedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr inserter = hz_cache_->makeInsertContext(lookup(RequestPath));
  inserter->insertHeaders(getResponseHeaders(), false);
  bool ready_for_next = true;
  while (ready_for_next) {
    inserter->insertBody(
      Buffer::OwnedImpl(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h')),
      [&](bool ready){ready_for_next = ready;}, false);
  }

  EXPECT_EQ(
      (HazelcastTestUtil::TEST_MAX_BODY_SIZE / HazelcastTestUtil::TEST_PARTITION_SIZE) +
      ((HazelcastTestUtil::TEST_MAX_BODY_SIZE % HazelcastTestUtil::TEST_PARTITION_SIZE) == 0 ? 0 : 1),
      testBodyMap().size());

  LookupContextPtr name_lookup_context = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(
      name_lookup_context.get(), std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h')));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, AbortInsertionIfKeyIsLocked) {
  const std::string RequestPath("/only/one/must/insert");

  LookupContextPtr name_lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  // The first missed lookup must be allowed to make insertion.
  ASSERT(!static_cast<HazelcastLookupContextBase&>(*name_lookup_context1).isAborted());

  // Following ones must abort the insertion.
  LookupContextPtr name_lookup_context2;
  std::thread t1([&] {
    // If the second lookup would not be performed in a separate thread, it will acquire
    // the lock even if it's already locked. This is because the key locks on Hazelcast
    // IMap are re-entrant. A locked key can be acquired by the same thread again and
    // again based on its pid.
    // TODO: Examine Envoy's threading model to ensure this case's safety.
    name_lookup_context2 = lookup(RequestPath);
  });
  t1.join();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  ASSERT(static_cast<HazelcastLookupContextBase&>(*name_lookup_context2).isAborted());

  const std::string Body("hazelcast");
  // second context should not insert even if arrives before the first one.
  insert(move(name_lookup_context2), getResponseHeaders(), Body);
  name_lookup_context2 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // first one must do the insertion.
  insert(move(name_lookup_context1), getResponseHeaders(), Body);
  name_lookup_context1 = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, MissLookupOnVersionMismatch) {
  const std::string RequestPath1("/miss/on/version/mismatch");

  LookupContextPtr name_lookup_context = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();

  const std::string Body(HazelcastTestUtil::TEST_PARTITION_SIZE * 2, 'h');
  insert(move(name_lookup_context), getResponseHeaders(), Body);
  name_lookup_context = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context.get(), Body));

  // Change version of the second partition.
  const std::string body2key = getBodyKey(variant_hash_key,1);
  auto body2 = testBodyMap().get(body2key);
  EXPECT_NE(body2, nullptr);
  body2->version(body2->version() + 1);
  testBodyMap().put(body2key, *body2);

  // Change happened in the second partition. Lookup to the first one should be successful.
  name_lookup_context = lookup(RequestPath1);
  std::string partition1 = getBody(*name_lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE);
  EXPECT_EQ(partition1, std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h'));

  std::string fullBody = getBody(*name_lookup_context, 0, HazelcastTestUtil::TEST_PARTITION_SIZE * 2);
  EXPECT_EQ(fullBody, HazelcastTestUtil::abortedBodyResponse());

  // Clean up must be performed for malformed entries.
  EXPECT_EQ(0, testBodyMap().size());
  EXPECT_EQ(0, testHeaderMap().size());

  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, MissLookupOnDifferentKey) {

  const std::string RequestPath("/miss/on/different/key");

  LookupContextPtr name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();

  const std::string Body("hazelcast");
  insert(move(name_lookup_context), getResponseHeaders(), Body);
  name_lookup_context = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context.get(), Body));

  // Manipulate the cache entry directly. Cache is not aware of that.
  // The cached key will not be the same with the created one by filter.
  auto header = testHeaderMap().get(mapKey(variant_hash_key));
  Key modified = header->variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  header->variantKey(std::move(modified));
  testHeaderMap().put(mapKey(variant_hash_key), *header);

  name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(name_lookup_context), getResponseHeaders(), Body);
  name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, testHeaderMap().size());
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, CleanUpCachedResponseOnMissingBody) {
  const std::string RequestPath1("/clean/up/on/missing/body");
  LookupContextPtr name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context1).variantHashKey();

  const std::string Body = std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h') +
      std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'z') +
      std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'c');

  insert(move(name_lookup_context1), getResponseHeaders(), Body);
  name_lookup_context1 = lookup(RequestPath1);

  // Response is cached with the following pattern:
  // variant_hash_key -> HeaderEntry (in header map)
  // variant_hash_key "0" -> Body1 (in body map)
  // variant_hash_key "1" -> Body2 (in body map)
  // variant_hash_key "2" -> Body3 (in body map)
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body));

  removeBody(variant_hash_key, 1); // evict Body2.

  name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);

  // Lookup for Body1 is OK.
  name_lookup_context1->getBody({0, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
    [](Buffer::InstancePtr&& data) {EXPECT_NE(data, nullptr);});

  // Lookup for Body2 must fail and trigger clean up.
  name_lookup_context1->getBody(
    {HazelcastTestUtil::TEST_PARTITION_SIZE, HazelcastTestUtil::TEST_PARTITION_SIZE * 3},
    [](Buffer::InstancePtr&& data){EXPECT_EQ(data, nullptr);});

  name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // On lookup miss, lock is being acquired. It must be released
  // explicitly or let context do the insertion and then release.
  // If not released, the second run for the test fails. Since no
  // insertion follows the missed lookup here, the lock is explicitly
  // unlocked.
  hz_cache_->unlock(variant_hash_key);

  // Assert clean up
  EXPECT_EQ(0, testBodyMap().size());
  EXPECT_EQ(0, testHeaderMap().size());
}

TEST_F(HazelcastDividedCacheTest, NotCreateBodyOnHeaderOnlyResponse) {
  const std::string RequestPath("/header/only/response");
  LookupContextPtr name_lookup_context = lookup(RequestPath);
  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  insert(move(name_lookup_context), getResponseHeaders(), "");
  name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  auto header = testHeaderMap().get(mapKey(variant_hash_key));
  EXPECT_NE(nullptr, header);
  EXPECT_EQ(0, header->bodySize());
  EXPECT_EQ(0, testBodyMap().size());
  clearMaps();
}

// Tests belong to SimpleHttpCache are applied below with minor changes on the test body.
TEST_F(HazelcastDividedCacheTest, SimplePutGet) {
  const std::string RequestPath1("/simple/put/first");
  LookupContextPtr name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast");
  insert(move(name_lookup_context1), getResponseHeaders(), Body1);

  name_lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body1));

  const std::string& RequestPath2("/simple/put/second");
  LookupContextPtr name_lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body2("hazelcast.distributed.caching");

  insert(move(name_lookup_context2), getResponseHeaders(), Body2);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(RequestPath2).get(), Body2));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, PrivateResponse) {
  const std::string request_path("/private/response");

  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body("Value");
  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup.
  insert(move(name_lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("/no/such/entry");
  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // Do not left over a missed lookup without inserting or releasing its lock.
  hz_cache_->unlock(variant_hash_key);
}

TEST_F(HazelcastDividedCacheTest, Fresh) {
  const Http::TestResponseHeaderMapImpl response_headers = {
    {"date", formatter_.fromTime(current_time_)},
    {"cache-control", "public, max-age=3600"}};
  insert("/", response_headers, "");
  time_source_.sleep(std::chrono::seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, Stale) {
  const Http::TestResponseHeaderMapImpl response_headers = {
    {"date", formatter_.fromTime(current_time_)},
    {"cache-control", "public, max-age=3600"}};
  insert("/", response_headers, "");
  time_source_.sleep(std::chrono::seconds(3601));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, RequestSmallMinFresh) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "min-fresh=1000");
  const std::string request_path("/request/small/min/fresh");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{
    {"date", formatter_.fromTime(current_time_)},
    {"age", "6000"},
    {"cache-control", "public, max-age=9000"}};
  const std::string Body("Value");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, ResponseStaleWithRequestLargeMaxStale) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "max-stale=9000");

  const std::string request_path("/response/stale/with/request/large/max/stale");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{
    {"date", formatter_.fromTime(current_time_)},
    {"age", "7200"},
    {"cache-control", "public, max-age=3600"}};

  const std::string Body("Value");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
  clearMaps();
}

TEST_F(HazelcastDividedCacheTest, StreamingPut) {
  InsertContextPtr inserter = hz_cache_->
    makeInsertContext(lookup("/streaming/put"));
  inserter->insertHeaders(getResponseHeaders(), false);
  inserter->insertBody(
    Buffer::OwnedImpl("Hello, "),
    [](bool ready){EXPECT_TRUE(ready); }, false);
  inserter->insertBody(Buffer::OwnedImpl("World!"), nullptr, true);
  LookupContextPtr name_lookup_context = lookup("/streaming/put");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  EXPECT_NE(nullptr, lookup_result_.headers_);
  ASSERT_EQ(13, lookup_result_.content_length_);
  EXPECT_EQ("Hello, World!", getBody(*name_lookup_context, 0, 13));
  EXPECT_EQ("o, World!", getBody(*name_lookup_context, 4, 13));
  clearMaps();
}

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.source.extensions.filters.http.cache.HazelcastHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  HazelcastHttpCacheConfig hz_cache_config = HazelcastTestUtil::getTestConfig(false);
  config.mutable_typed_config()->PackFrom(hz_cache_config);
  HazelcastHttpCache& cache = static_cast<HazelcastHttpCache&>(factory->getCache(config));
  EXPECT_EQ(cache.cacheInfo().name_, "envoy.extensions.http.cache.hazelcast");

  // Explicitly destroy Hazelcast connection here. Otherwise the test
  // environment does not wait for cache destructor to be completed
  // and this causes segfault when Hazelcast Client is shutting down.
  cache.shutdown();
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
