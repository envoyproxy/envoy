#include "envoy/registry/registry.h"

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_context.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/test_util.h"
#include "test/test_common/logging.h"

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
    HazelcastHttpCacheConfig typed_config = HazelcastTestUtil::getTestTypedConfig(GetParam());
    envoy::extensions::filters::http::cache::v3alpha::CacheConfig cache_config =
        HazelcastTestUtil::getTestCacheConfig();
    cache_ = std::make_unique<HazelcastHttpCache>(std::move(typed_config), cache_config);
    // To test the cache with a real Hazelcast instance, use remote test accessor.
    // cache_->start(HazelcastTestUtil::getTestRemoteAccessor(*cache_));
    cache_->start(std::make_unique<LocalTestAccessor>());
    getTestAccessor().clearMaps();
  }

  Key getVariantKey(LookupContextPtr& lookup,
                    std::vector<std::pair<std::string, std::string>>& headers) {
    HazelcastLookupContextBase& hz_lookup = static_cast<HazelcastLookupContextBase&>(*lookup);
    Key variant_key = hz_lookup.variantKey();
    hz_lookup.arrangeVariantHeaders(variant_key, headers);
    return variant_key;
  }
};

INSTANTIATE_TEST_SUITE_P(CommonCacheTests, HazelcastHttpCacheTest, ::testing::Bool());

TEST_P(HazelcastHttpCacheTest, MissPutAndGetEntries) {
  // To test divided body behavior as well, bodies having sizes near the limit are preferred.
  const std::string RequestPath1("/body/with/limit/size/plus/one");
  const std::string RequestPath2("/body/with/exact/limit/size");
  const std::string RequestPath3("/body/with/limit/size/minus/one");

  LookupContextPtr lookup_context1 = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context2 = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  LookupContextPtr lookup_context3 = lookup(RequestPath3);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  int length = HazelcastTestUtil::TEST_PARTITION_SIZE * 2;
  const std::string Body1(length + 1, 's');
  absl::string_view Body2(Body1.c_str(), length);
  absl::string_view Body3(Body1.c_str(), length - 1);

  insert(move(lookup_context1), getResponseHeaders(), Body1);
  insert(move(lookup_context2), getResponseHeaders(), Body2);
  insert(move(lookup_context3), getResponseHeaders(), Body3);

  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath1).get(), Body1));
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath2).get(), Body2));
  EXPECT_TRUE(expectLookupSuccessWithFullBody(lookup(RequestPath3).get(), Body3));
  EXPECT_EQ(GetParam(), cache_->unified());
}

TEST_P(HazelcastHttpCacheTest, HandleRangedResponses) {
  const std::string RequestPath("/ranged/responses");
  LookupContextPtr lookup_context = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  int size = HazelcastTestUtil::TEST_PARTITION_SIZE;
  const std::string Body = std::string(size, 'h') + std::string(size, 'z') + std::string(size, 'c');
  insert(move(lookup_context), getResponseHeaders(), Body);
  lookup_context = lookup(RequestPath);

  // 'h' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str(), size), getBody(*lookup_context, 0, size));

  // 'z' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str() + size, size), getBody(*lookup_context, size, size * 2));

  // 'h' * (size/2) + 'z' * (size/2)
  EXPECT_EQ(absl::string_view(Body.c_str() + size / 2, size),
            getBody(*lookup_context, size / 2, size + size / 2));

  // 'h' + 'z' * (size) + 'c'
  EXPECT_EQ(absl::string_view(Body.c_str() + size - 1, size + 2),
            getBody(*lookup_context, size - 1, 2 * size + 1));

  // 'h' * (size) + 'z' * (size) + 'c' * (size)
  EXPECT_EQ(absl::string_view(Body.c_str(), size * 3), getBody(*lookup_context, 0, size * 3));
}

TEST_P(HazelcastHttpCacheTest, VariantKeyTest) {
  // The same key should be created for the same vary headers even if their
  // order is different.
  LookupContextPtr lookup_context = lookup("/variant/key/test/");
  std::vector<std::pair<std::string, std::string>> vary_headers;
  vary_headers.push_back(std::make_pair("Accept-Language", "tr;q=0.8"));
  vary_headers.push_back(std::make_pair("User-Agent", "desktop"));
  auto key1 = getVariantKey(lookup_context, vary_headers);
  vary_headers.clear();
  vary_headers.push_back(std::make_pair("User-Agent", "desktop"));
  vary_headers.push_back(std::make_pair("Accept-Language", "tr;q=0.8"));
  auto key2 = getVariantKey(lookup_context, vary_headers);

  EXPECT_EQ(4, key1.custom_fields_size()); // 2 keys, 2 values, 4 in total
  EXPECT_EQ(4, key2.custom_fields_size());
  EXPECT_TRUE(Envoy::Protobuf::util::MessageDifferencer::Equals(key1, key2));
  EXPECT_EQ(stableHashKey(key1), stableHashKey(key2));
}

//
// Tests belong to SimpleHttpCache are applied below with minor changes on the test bodies.
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
  uint64_t variant_key_hash =
      static_cast<HazelcastLookupContextBase&>(*name_lookup_context).variantKeyHash();
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  // Do not left over a missed lookup without inserting or releasing its lock.
  // This is required for remote accessor.
  cache_->unlock(variant_key_hash);
}

TEST_P(HazelcastHttpCacheTest, Fresh) {
  insert("/", getResponseHeaders(), "");
  time_source_.advanceTimeWait(std::chrono::seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_P(HazelcastHttpCacheTest, Stale) {
  insert("/", getResponseHeaders(), "");
  time_source_.advanceTimeWait(std::chrono::seconds(3601));
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
  InsertContextPtr inserter = cache_->makeInsertContext(lookup("/streaming/put"));
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

TEST_P(HazelcastHttpCacheTest, CacheStartShutdown) {
  EXPECT_LOG_CONTAINS("warn", "Client is already connected.",
                      cache_->start(std::make_unique<LocalTestAccessor>()));
  cache_->shutdown(false);
  EXPECT_LOG_CONTAINS("warn", "Hazelcast client is already disconnected.", cache_->shutdown(true));
}

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.source.extensions.filters.http.cache.HazelcastHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  HazelcastHttpCacheConfig typed_config = HazelcastTestUtil::getTestTypedConfig(true);
  typed_config.set_group_name("do-not-connect-any-cluster");
  typed_config.set_connection_attempt_limit(1);
  typed_config.set_connection_attempt_period(1); // give up immediately.
  ClientConfig client_config = ConfigUtil::getClientConfig(typed_config);
  config.mutable_typed_config()->PackFrom(typed_config);

  {
    // getOfflineCache() call is for testing. It creates a HazelcastHttpCache but does
    // not make it operational until a start() call. This is required to make cacheInfo()
    // behavior testable when using local accessor.
    HazelcastHttpCachePtr cache =
        static_cast<HazelcastHttpCacheFactory*>(factory)->getOfflineCache(config);
    EXPECT_EQ(cache->cacheInfo().name_, "envoy.extensions.http.cache.hazelcast");

    StorageAccessorPtr accessor = std::make_unique<HazelcastClusterAccessor>(
        *cache, std::move(client_config), cache->prefix(), cache->bodySizePerEntry());
    EXPECT_THROW_WITH_MESSAGE(cache->start(std::move(accessor)), EnvoyException,
                              "Hazelcast Client could not connect to any cluster.");
  }
  EXPECT_THROW_WITH_MESSAGE(factory->getCache(config), EnvoyException,
                            "Hazelcast Client could not connect to any cluster.");
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
