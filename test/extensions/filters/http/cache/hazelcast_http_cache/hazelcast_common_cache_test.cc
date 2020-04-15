#include "envoy/registry/registry.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Common tests for both DIVIDED and UNIFIED cache mode.
 */
class HazelcastHttpCacheTest : public HazelcastHttpCacheTestBase,
                               public testing::WithParamInterface<bool> {
protected:
  void SetUp() override {
    HazelcastHttpCacheConfig config = HazelcastTestUtil::getTestConfig(GetParam());
    // To test the cache with a real Hazelcast instance, remote cache
    // must be used during tests.
    // cache_ = std::make_unique<HazelcastTestableRemoteCache>(config);
    cache_ = std::make_unique<HazelcastTestableLocalCache>(config);
    cache_->restoreTestConnection();
    cache_->clearTestMaps();
  }
};

INSTANTIATE_TEST_SUITE_P(CommonCacheTests, HazelcastHttpCacheTest, ::testing::Bool());

TEST_P(HazelcastHttpCacheTest, MissPutAndGetEntries) {
  // To test divided body behavior as well, bodies having sizes near the limit are preferred.
  const std::string RequestPath1("/body/with/limit/size/plus/one");
  const std::string RequestPath2("/body/with/exactly/limit/size");
  const std::string RequestPath3("/body/with/limit/size/minus/one");

  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context3 = lookup(RequestPath3);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  int length = HazelcastTestUtil::TEST_PARTITION_SIZE * 2;
  const std::string Body1("s", length + 1);
  absl::string_view Body2(Body1.c_str(), length);
  absl::string_view Body3(Body1.c_str(), length - 1);

  insert(move(lookup_context1), getResponseHeaders(), Body1);
  insert(move(lookup_context2), getResponseHeaders(), Body2);
  insert(move(lookup_context3), getResponseHeaders(), Body3);

  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath1).get(), Body1));
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath2).get(), Body2));
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath3).get(), Body3));
}

TEST_P(HazelcastHttpCacheTest, HandleRangedResponses) {
  const std::string RequestPath("/ranged/responses");

  LookupContextPtr lookup_context1 = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  int size = HazelcastTestUtil::TEST_PARTITION_SIZE;
  const std::string Body1 = std::string(size, 'h');
  const std::string Body2 = std::string(size, 'z');
  const std::string Body3 = std::string(size, 'c');
  const std::string Body = Body1 + Body2 + Body3;

  insert(move(lookup_context1), getResponseHeaders(), Body);
  lookup_context1 = lookup(RequestPath);

  // 'h' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str(), size), getBody(*lookup_context1, 0, size));

  // 'z' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str() + size, size),
            getBody(*lookup_context1, size, size * 2));

  // 'h' * (size/2) + 'z' * (size/2)
  EXPECT_EQ(absl::string_view(Body.c_str() + size / 2, size),
            getBody(*lookup_context1, size / 2, size + size / 2));

  // 'h' + 'z' * (size) + 'c'
  EXPECT_EQ(absl::string_view(Body.c_str() + size - 1, size + 2),
            getBody(*lookup_context1, size - 1, 2 * size + 1));

  // 'h' * (size) + 'z' * (size) + 'c' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str(), size * 3), getBody(*lookup_context1, 0, size * 3));
}

//
// Tests belong to SimpleHttpCache are applied below with minor changes on the test body.
//
TEST_P(HazelcastHttpCacheTest, SimplePutGet) {
  const std::string RequestPath1("/simple/put/first");
  LookupContextPtr name_lookup_context = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body1("hazelcast");
  insert(move(name_lookup_context), getResponseHeaders(), Body1);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath1).get(), Body1));

  const std::string RequestPath2("/simple/put/second");
  name_lookup_context = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body2("hazelcast.http.cache");
  insert(move(name_lookup_context), getResponseHeaders(), Body2);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath2).get(), Body2));
}

TEST_P(HazelcastHttpCacheTest, PrivateResponse) {
  const std::string request_path("/private/response");

  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body("Value");

  insert(move(name_lookup_context), getResponseHeaders(), Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(request_path).get(), Body));
}

TEST_P(HazelcastHttpCacheTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("/no/such/entry");
  uint64_t variant_hash_key =
      static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantHashKey();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // Do not left over a missed lookup without inserting or releasing its lock.
  cache_->base().unlock(variant_hash_key);
}

TEST_P(HazelcastHttpCacheTest, Fresh) {
  insert("/", getResponseHeaders(), "");
  time_source_.sleep(std::chrono::seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_P(HazelcastHttpCacheTest, Stale) {
  insert("/", getResponseHeaders(), "");
  time_source_.sleep(std::chrono::seconds(3601));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_P(HazelcastHttpCacheTest, RequestSmallMinFresh) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "min-fresh=1000");
  const std::string request_path("/request/small/min/fresh");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "6000"},
                                                   {"cache-control", "public, max-age=9000"}};
  const std::string Body("content");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(request_path).get(), Body));
}

TEST_P(HazelcastHttpCacheTest, ResponseStaleWithRequestLargeMaxStale) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "max-stale=9000");

  const std::string request_path("/response/stale/with/request/large/max/stale");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "7200"},
                                                   {"cache-control", "public, max-age=3600"}};

  const std::string Body("content");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(request_path).get(), Body));
}

TEST_P(HazelcastHttpCacheTest, StreamingPutAndRangeGet) {
  InsertContextPtr inserter = cache_->base().makeInsertContext(lookup("/streaming/put"));
  inserter->insertHeaders(getResponseHeaders(), false);
  inserter->insertBody(
      Buffer::OwnedImpl("Hello, "), [](bool ready) { EXPECT_TRUE(ready); }, false);
  inserter->insertBody(Buffer::OwnedImpl("World!"), nullptr, true);
  LookupContextPtr name_lookup_context = lookup("/streaming/put");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  EXPECT_NE(nullptr, lookup_result_.headers_);
  ASSERT_EQ(13, lookup_result_.content_length_);
  EXPECT_EQ("Hello, World!", getBody(*name_lookup_context, 0, 13));
  EXPECT_EQ("o, World!", getBody(*name_lookup_context, 4, 13));
}

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.source.extensions.filters.http.cache.HazelcastHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  HazelcastHttpCacheConfig hz_cache_config = HazelcastTestUtil::getTestConfig(true);
  hz_cache_config.set_group_name("do-not-connect-any-cluster");
  hz_cache_config.set_connection_attempt_limit(1);
  hz_cache_config.set_connection_attempt_period(1); // give up immediately.
  config.mutable_typed_config()->PackFrom(hz_cache_config);

  // getOfflineCache() call is for testing. It creates a HazelcastHttpCache but does
  // not make it operational until a connect() call.
  HttpCache& cache = static_cast<HazelcastHttpCacheFactory*>(factory)->getOfflineCache(config);
  EXPECT_EQ(cache.cacheInfo().name_, "envoy.extensions.http.cache.hazelcast");
  EXPECT_THROW_WITH_MESSAGE(static_cast<HazelcastHttpCache&>(cache).connect(), EnvoyException,
                            "Hazelcast Client could not connect to any cluster.");
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
