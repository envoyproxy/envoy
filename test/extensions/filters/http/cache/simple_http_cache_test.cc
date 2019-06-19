#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/cache/simple_http_cache.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

const std::string kEpochDate = "Thu, 01 Jan 1970 00:00:00 GMT";

class SimpleHttpCacheTest : public testing::Test {
protected:
  SimpleHttpCacheTest() {
    request_headers_.insertMethod().value(absl::string_view(("GET")));
    request_headers_.insertHost().value(absl::string_view(("example.com")));
    request_headers_.insertScheme().value(absl::string_view(("https")));
    request_headers_.insertCacheControl().value(absl::string_view(("max-age=3600")));
  }

  // Performs a cache lookup.
  LookupContextPtr lookup(absl::string_view request_path) {
    LookupRequest request = makeLookupRequest(request_path);
    LookupContextPtr context = cache_.makeLookupContext(std::move(request));
    context->getHeaders([this](LookupResult&& result) { lookup_result_ = std::move(result); });
    return context;
  }

  // Inserts a value into the cache.
  void insert(LookupContextPtr lookup, const Http::TestHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    InsertContextPtr inserter = cache_.makeInsertContext(move(lookup));
    inserter->insertHeaders(response_headers, false);
    inserter->insertBody(Buffer::OwnedImpl(response_body), nullptr, true);
  }

  void insert(absl::string_view request_path, const Http::TestHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    insert(lookup(request_path), response_headers, response_body);
  }

  std::string getBody(LookupContext& context, uint64_t start, uint64_t end) {
    AdjustedByteRange range(start, end);
    std::string body;
    context.getBody(range, [&body](Buffer::InstancePtr&& data) {
      EXPECT_NE(data, nullptr);
      if (data) {
        body = data->toString();
      }
    });
    return body;
  }

  LookupRequest makeLookupRequest(absl::string_view request_path) {
    request_headers_.insertPath().value(request_path);
    return LookupRequest(request_headers_, current_time_);
  };

  AssertionResult ExpectLookupSuccessWithBody(LookupContext* lookup_context,
                                              absl::string_view body) {
    if (lookup_result_.cache_entry_status != CacheEntryStatus::Ok) {
      return AssertionFailure() << "Expected: lookup_result_.cache_entry_status == "
                                   "CacheEntryStatus::Ok\n  Actual: "
                                << lookup_result_.cache_entry_status;
    }
    if (!lookup_result_.headers) {
      return AssertionFailure() << "Expected nonnull lookup_result_.headers";
    }
    if (!lookup_context) {
      return AssertionFailure() << "Expected nonnull lookup_context";
    }
    const std::string actual_body = getBody(*lookup_context, 0, body.size() - 1u);
    if (body != actual_body) {
      return AssertionFailure() << "Expected body == " << body << "\n  Actual:  " << actual_body;
    }
    return AssertionSuccess();
  }

  SimpleHttpCache cache_;
  LookupResult lookup_result_;
  Http::TestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
};

// Simple flow of putting in an item, getting it, deleting it.
TEST_F(SimpleHttpCacheTest, PutGet) {
  const std::string kRequestPath1("Name");
  LookupContextPtr name_lookup_context = lookup(kRequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);

  Http::TestHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                           {"cache-control", "public, max-age=3600"}};

  const std::string kBody1("Value");
  insert(move(name_lookup_context), response_headers, kBody1);
  name_lookup_context = lookup(kRequestPath1);
  EXPECT_TRUE(ExpectLookupSuccessWithBody(name_lookup_context.get(), kBody1));

  const std::string& kRequestPath2("Another Name");
  LookupContextPtr another_name_lookup_context = lookup(kRequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);

  const std::string kNewBody1("NewValue");
  insert(move(name_lookup_context), response_headers, kNewBody1);
  EXPECT_TRUE(ExpectLookupSuccessWithBody(lookup(kRequestPath1).get(), kNewBody1));
}

TEST_F(SimpleHttpCacheTest, PrivateResponse) {
  Http::TestHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                           {"age", "2"},
                                           {"cache-control", "private, max-age=3600"}};
  const std::string request_path("Name");

  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);

  const std::string kBody("Value");
  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup.
  insert(move(name_lookup_context), response_headers, kBody);
  EXPECT_TRUE(ExpectLookupSuccessWithBody(lookup(request_path).get(), kBody));
}

TEST_F(SimpleHttpCacheTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("Name");
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);
}

class SimpleHttpCacheTest1Response
    : public SimpleHttpCacheTest,
      public testing::WithParamInterface<
          testing::tuple<CacheEntryStatus, Http::TestHeaderMapImpl>> {
protected:
  CacheEntryStatus expectedStatus() const { return testing::get<CacheEntryStatus>(GetParam()); }
  const Http::TestHeaderMapImpl& responseHeaders() const {
    return testing::get<Http::TestHeaderMapImpl>(GetParam());
  }
};

TEST_P(SimpleHttpCacheTest1Response, Expired) {
  Http::TestHeaderMapImpl response_headers = responseHeaders();
  // TODO(toddmgreer) Test with various date headers.
  response_headers.addCopy("date", formatter_.fromTime(current_time_));
  insert("/", response_headers, "");
  lookup("/");
  EXPECT_EQ(expectedStatus(), lookup_result_.cache_entry_status);
}

const std::vector<Http::TestHeaderMapImpl> expired_headers = {
    {{"date", "Thu, 01 Jan 2100 00:00:00 GMT"}, {"cache-control", "public, max-age=3600"}},
    {{"cache-control", "public, s-max-age=-1"}},
    {{"cache-control", "public, max-age=-1"}},
    {{"date", "foo"}},
    {{"date", kEpochDate}, {"expires", "foo"}},
    {{"expires", kEpochDate}, {"cache-control", "public"}},
    {{"age", "2"}, {"cache-control", "public"}},
    {{"age", "6000"}}};

const std::vector<Http::TestHeaderMapImpl> ok_headers = {
    {{"cache-control", "public, max-age=3600"}}};

INSTANTIATE_TEST_SUITE_P(Expired, SimpleHttpCacheTest1Response,
                         testing::Combine(testing::Values(CacheEntryStatus::RequiresValidation),
                                          testing::ValuesIn(expired_headers)));

INSTANTIATE_TEST_SUITE_P(Ok, SimpleHttpCacheTest1Response,
                         testing::Combine(testing::Values(CacheEntryStatus::Ok),
                                          testing::ValuesIn(ok_headers)));

TEST_F(SimpleHttpCacheTest, RequestSmallMinFresh) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "min-fresh=1000");
  const std::string request_path("Name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);

  Http::TestHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                           {"age", "6000"},
                                           {"cache-control", "public, max-age=9000"}};
  const std::string kBody("Value");
  insert(move(name_lookup_context), response_headers, kBody);
  EXPECT_TRUE(ExpectLookupSuccessWithBody(lookup(request_path).get(), kBody));
}

TEST_F(SimpleHttpCacheTest, ResponseStaleWithRequestLargeMaxStale) {
  request_headers_.setReferenceKey(Http::Headers::get().CacheControl, "max-stale=9000");

  const std::string request_path("Name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status);

  Http::TestHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                           {"age", "7200"},
                                           {"cache-control", "public, max-age=3600"}};

  const std::string kBody("Value");
  insert(move(name_lookup_context), response_headers, kBody);
  EXPECT_TRUE(ExpectLookupSuccessWithBody(lookup(request_path).get(), kBody));
}

TEST_F(SimpleHttpCacheTest, StreamingPut) {
  Http::TestHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                           {"age", "2"},
                                           {"cache-control", "public, max-age=3600"}};
  InsertContextPtr inserter = cache_.makeInsertContext(lookup("request_path"));
  inserter->insertHeaders(response_headers, false);
  inserter->insertBody(
      Buffer::OwnedImpl("Hello, "), [](bool ready) { EXPECT_TRUE(ready); }, false);
  inserter->insertBody(Buffer::OwnedImpl("World!"), nullptr, true);
  LookupContextPtr name_lookup_context = lookup("request_path");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status);
  EXPECT_NE(nullptr, lookup_result_.headers);
  ASSERT_EQ(13, lookup_result_.content_length);
  EXPECT_EQ("Hello, World!", getBody(*name_lookup_context, 0, 12));
}

TEST(Registration, getFactory) {
  HttpCacheFactory* factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactory("SimpleHttpCache");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->getCache().cacheInfo().name_, "SimpleHttpCache");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
