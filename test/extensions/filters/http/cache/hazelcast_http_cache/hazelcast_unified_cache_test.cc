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

class HazelcastUnifiedCacheTest : public HazelcastHttpCacheTestBase {
protected:
  static void SetUpTestSuite() {
    HazelcastHttpCacheConfig cfg = HazelcastTestUtil::getTestConfig(true);
    hz_cache_ = new HazelcastHttpCache(cfg);
    hz_cache_->connect();
    clearMaps();
  }
};

TEST_F(HazelcastUnifiedCacheTest, SimpleMissAndPutEntries) {
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

TEST_F(HazelcastUnifiedCacheTest, HandleRangedResponses) {
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

TEST_F(HazelcastUnifiedCacheTest, AbortUnifiedInsertionWhenMaxSizeReached) {
  const std::string RequestPath("/abort/when/max/size/reached");
  InsertContextPtr inserter = hz_cache_->makeInsertContext(lookup(RequestPath));
  inserter->insertHeaders(getResponseHeaders(), false);
  bool ready_for_next = true;
  while (ready_for_next) {
    inserter->insertBody(
      Buffer::OwnedImpl(std::string(HazelcastTestUtil::TEST_PARTITION_SIZE / 3, 'h')),
      [&](bool ready){ready_for_next = ready;}, false);
  }

  LookupContextPtr name_lookup_context = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(
      name_lookup_context.get(), std::string(HazelcastTestUtil::TEST_PARTITION_SIZE, 'h')));
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, PutResponseIfAbsent) {
  const std::string RequestPath("/only/one/must/insert");

  LookupContextPtr name_lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  LookupContextPtr name_lookup_context2 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast");
  const std::string Body2("hazelcast.distributed.caching");

  // The second context should insert if the cache is empty for this request.
  insert(move(name_lookup_context1), getResponseHeaders(), Body1);
  name_lookup_context1 = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body1));

  // The first context should not do the insertion/override the existing value.
  insert(move(name_lookup_context2), getResponseHeaders(), Body2);
  // Response body must remain as Body1
  name_lookup_context2 = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context2.get(), Body1));
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, DoNotOverrideExistingResponse) {
  const std::string RequestPath1("/on/unified/not/override");

  LookupContextPtr name_lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr name_lookup_context2 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast-first");
  const std::string Body2("hazelcast-second");

  insert(move(name_lookup_context1), getResponseHeaders(), Body1);
  insert(move(name_lookup_context2), getResponseHeaders(), Body2);

  name_lookup_context1 = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context1.get(), Body1));
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, HeaderOnlyResponse) {
  InsertContextPtr inserter = hz_cache_->makeInsertContext(lookup("/header/only"));
  inserter->insertHeaders(getResponseHeaders(), true);
  LookupContextPtr name_lookup_context = lookup("/header/only");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  EXPECT_EQ(0, lookup_result_.content_length_);
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, MissLookupOnDifferentKey) {

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
  auto response = testResponseMap().get(mapKey(variant_hash_key));
  Key modified = response->header().variantKey();
  modified.add_custom_fields("custom1");
  modified.add_custom_fields("custom2");
  response->header().variantKey(std::move(modified));
  testResponseMap().put(mapKey(variant_hash_key), *response);

  name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // New entry insertion should be aborted and not override the existing one with the
  // same hash key. This scenario is possible if there is a hash collision. No eviction
  // or clean up is expected. Since overriding an entry is prevented.
  insert(move(name_lookup_context), getResponseHeaders(), Body);
  name_lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  EXPECT_EQ(1, testResponseMap().size());
  clearMaps();
}

// Tests belong to SimpleHttpCache are applied below with minor changes on the test body.
TEST_F(HazelcastUnifiedCacheTest, SimplePutGet) {
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

TEST_F(HazelcastUnifiedCacheTest, PrivateResponse) {
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

TEST_F(HazelcastUnifiedCacheTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("/no/such/entry");
  uint64_t variant_hash_key =
    static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // Do not left over a missed lookup without inserting or releasing its lock.
  hz_cache_->unlock(variant_hash_key);
}

TEST_F(HazelcastUnifiedCacheTest, Fresh) {
  const Http::TestResponseHeaderMapImpl response_headers = {
    {"date", formatter_.fromTime(current_time_)},
    {"cache-control", "public, max-age=3600"}};
  insert("/", response_headers, "");
  time_source_.sleep(std::chrono::seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, Stale) {
  const Http::TestResponseHeaderMapImpl response_headers = {
    {"date", formatter_.fromTime(current_time_)},
    {"cache-control", "public, max-age=3600"}};
  insert("/", response_headers, "");
  time_source_.sleep(std::chrono::seconds(3601));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  clearMaps();
}

TEST_F(HazelcastUnifiedCacheTest, RequestSmallMinFresh) {
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

TEST_F(HazelcastUnifiedCacheTest, ResponseStaleWithRequestLargeMaxStale) {
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

TEST_F(HazelcastUnifiedCacheTest, StreamingPut) {
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
  HazelcastHttpCacheConfig hz_cache_config = HazelcastTestUtil::getTestConfig(true);
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
