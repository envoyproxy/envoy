#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

const std::string EpochDate = "Thu, 01 Jan 1970 00:00:00 GMT";

envoy::extensions::filters::http::cache::v3alpha::CacheConfig getConfig() {
  // Allows 'accept' to be varied in the tests.
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
  add_accept->set_exact("accept");
  return config;
}

class SimpleHttpCacheTest : public testing::Test {
protected:
  SimpleHttpCacheTest() : vary_allow_list_(getConfig().allowed_vary_headers()) {
    request_headers_.setMethod("GET");
    request_headers_.setHost("example.com");
    request_headers_.setScheme("https");
    request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
  }

  // Performs a cache lookup.
  LookupContextPtr lookup(absl::string_view request_path) {
    LookupRequest request = makeLookupRequest(request_path);
    LookupContextPtr context = cache_.makeLookupContext(std::move(request));
    context->getHeaders([this](LookupResult&& result) { lookup_result_ = std::move(result); });
    return context;
  }

  // Inserts a value into the cache.
  void insert(LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    InsertContextPtr inserter = cache_.makeInsertContext(move(lookup));
    const ResponseMetadata metadata = {current_time_};
    inserter->insertHeaders(response_headers, metadata, false);
    inserter->insertBody(Buffer::OwnedImpl(response_body), nullptr, true);
  }

  void insert(absl::string_view request_path,
              const Http::TestResponseHeaderMapImpl& response_headers,
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
    request_headers_.setPath(request_path);
    return LookupRequest(request_headers_, current_time_, vary_allow_list_);
  }

  AssertionResult expectLookupSuccessWithBody(LookupContext* lookup_context,
                                              absl::string_view body) {
    if (lookup_result_.cache_entry_status_ != CacheEntryStatus::Ok) {
      return AssertionFailure() << "Expected: lookup_result_.cache_entry_status == "
                                   "CacheEntryStatus::Ok\n  Actual: "
                                << lookup_result_.cache_entry_status_;
    }
    if (!lookup_result_.headers_) {
      return AssertionFailure() << "Expected nonnull lookup_result_.headers";
    }
    if (!lookup_context) {
      return AssertionFailure() << "Expected nonnull lookup_context";
    }
    const std::string actual_body = getBody(*lookup_context, 0, body.size());
    if (body != actual_body) {
      return AssertionFailure() << "Expected body == " << body << "\n  Actual:  " << actual_body;
    }
    return AssertionSuccess();
  }

  SimpleHttpCache cache_;
  LookupResult lookup_result_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  VaryHeader vary_allow_list_;
};

// Simple flow of putting in an item, getting it, deleting it.
TEST_F(SimpleHttpCacheTest, PutGet) {
  const std::string RequestPath1("Name");
  LookupContextPtr name_lookup_context = lookup(RequestPath1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"cache-control", "public,max-age=3600"}};

  const std::string Body1("Value");
  insert(move(name_lookup_context), response_headers, Body1);
  name_lookup_context = lookup(RequestPath1);
  EXPECT_TRUE(expectLookupSuccessWithBody(name_lookup_context.get(), Body1));

  const std::string& RequestPath2("Another Name");
  LookupContextPtr another_name_lookup_context = lookup(RequestPath2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string NewBody1("NewValue");
  insert(move(name_lookup_context), response_headers, NewBody1);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(RequestPath1).get(), NewBody1));
}

TEST_F(SimpleHttpCacheTest, PrivateResponse) {
  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "2"},
                                                   {"cache-control", "private,max-age=3600"}};
  const std::string request_path("Name");

  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string Body("Value");
  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup.
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
}

TEST_F(SimpleHttpCacheTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("Name");
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

TEST_F(SimpleHttpCacheTest, Fresh) {
  const Http::TestResponseHeaderMapImpl response_headers = {
      {"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}};
  // TODO(toddmgreer): Test with various date headers.
  insert("/", response_headers, "");
  time_source_.advanceTimeWait(Seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_F(SimpleHttpCacheTest, Stale) {
  const Http::TestResponseHeaderMapImpl response_headers = {
      {"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}};
  // TODO(toddmgreer): Test with various date headers.
  insert("/", response_headers, "");
  time_source_.advanceTimeWait(Seconds(3601));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_F(SimpleHttpCacheTest, RequestSmallMinFresh) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "min-fresh=1000");
  const std::string request_path("Name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "6000"},
                                                   {"cache-control", "public, max-age=9000"}};
  const std::string Body("Value");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
}

TEST_F(SimpleHttpCacheTest, ResponseStaleWithRequestLargeMaxStale) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-stale=9000");

  const std::string request_path("Name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "7200"},
                                                   {"cache-control", "public, max-age=3600"}};

  const std::string Body("Value");
  insert(move(name_lookup_context), response_headers, Body);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(request_path).get(), Body));
}

TEST_F(SimpleHttpCacheTest, StreamingPut) {
  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"age", "2"},
                                                   {"cache-control", "public, max-age=3600"}};
  InsertContextPtr inserter = cache_.makeInsertContext(lookup("request_path"));
  const ResponseMetadata metadata = {current_time_};
  inserter->insertHeaders(response_headers, metadata, false);
  inserter->insertBody(
      Buffer::OwnedImpl("Hello, "), [](bool ready) { EXPECT_TRUE(ready); }, false);
  inserter->insertBody(Buffer::OwnedImpl("World!"), nullptr, true);
  LookupContextPtr name_lookup_context = lookup("request_path");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  EXPECT_NE(nullptr, lookup_result_.headers_);
  ASSERT_EQ(13, lookup_result_.content_length_);
  EXPECT_EQ("Hello, World!", getBody(*name_lookup_context, 0, 13));
}

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.cache.simple_http_cache.v3alpha.SimpleHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  config.mutable_typed_config()->PackFrom(*factory->createEmptyConfigProto());
  EXPECT_EQ(factory->getCache(config).cacheInfo().name_, "envoy.extensions.http.cache.simple");
}

TEST_F(SimpleHttpCacheTest, VaryResponses) {
  // Responses will vary on accept.
  const std::string RequestPath("some-resource");
  Http::TestResponseHeaderMapImpl response_headers{{"date", formatter_.fromTime(current_time_)},
                                                   {"cache-control", "public,max-age=3600"},
                                                   {"vary", "accept"}};

  // First request.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "image/*");
  LookupContextPtr first_value_vary = lookup(RequestPath);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  const std::string Body1("accept is image/*");
  insert(move(first_value_vary), response_headers, Body1);
  first_value_vary = lookup(RequestPath);
  EXPECT_TRUE(expectLookupSuccessWithBody(first_value_vary.get(), Body1));

  // Second request with a different value for the varied header.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "text/html");
  LookupContextPtr second_value_vary = lookup(RequestPath);
  // Should miss because we don't have this version of the response saved yet.
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  // Add second version and make sure we receive the correct one..
  const std::string Body2("accept is text/html");
  insert(move(second_value_vary), response_headers, Body2);
  EXPECT_TRUE(expectLookupSuccessWithBody(lookup(RequestPath).get(), Body2));

  // Looks up first version again to be sure it wasn't replaced with the second one.
  EXPECT_TRUE(expectLookupSuccessWithBody(first_value_vary.get(), Body1));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
