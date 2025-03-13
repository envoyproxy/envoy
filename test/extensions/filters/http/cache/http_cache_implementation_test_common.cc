#include "test/extensions/filters/http/cache/http_cache_implementation_test_common.h"

#include <future>
#include <string>
#include <utility>

#include "source/common/common/assert.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"

using ::envoy::extensions::filters::http::cache::v3::CacheConfig;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Not;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {

CacheConfig getConfig() {
  // Allows 'accept' to be varied in the tests.
  CacheConfig config;
  config.add_allowed_vary_headers()->set_exact("accept");

  return config;
}

MATCHER(IsOk, "") { return arg.ok(); }

} // namespace

void HttpCacheTestDelegate::pumpDispatcher() {
  // There may be multiple steps in a cache operation going back and forth with work
  // on a cache's thread and work on the filter's thread. So drain both things up to
  // 10 times each. This number is arbitrary and could be increased if necessary for
  // a cache implementation.
  for (int i = 0; i < 10; i++) {
    beforePumpingDispatcher();
    dispatcher().run(Event::Dispatcher::RunType::Block);
  }
}

HttpCacheImplementationTest::HttpCacheImplementationTest()
    : delegate_(GetParam()()),
      vary_allow_list_(getConfig().allowed_vary_headers(), factory_context_) {
  request_headers_.setMethod("GET");
  request_headers_.setHost("example.com");
  request_headers_.setScheme("https");
  request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
  ON_CALL(encoder_callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher()));
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher()));
  delegate_->setUp();
}

HttpCacheImplementationTest::~HttpCacheImplementationTest() {
  Assert::resetEnvoyBugCountersForTest();

  delegate_->tearDown();
}

bool HttpCacheImplementationTest::updateHeaders(
    absl::string_view request_path, const Http::TestResponseHeaderMapImpl& response_headers,
    const ResponseMetadata& metadata) {
  LookupContextPtr lookup_context = lookup(request_path);
  bool captured_result = false;
  bool seen_result = false;
  cache()->updateHeaders(*lookup_context, response_headers, metadata,
                         [&captured_result, &seen_result](bool result) {
                           captured_result = result;
                           seen_result = true;
                         });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return captured_result;
}

LookupContextPtr HttpCacheImplementationTest::lookup(absl::string_view request_path) {
  LookupRequest request = makeLookupRequest(request_path);
  LookupContextPtr context = cache()->makeLookupContext(std::move(request), decoder_callbacks_);
  bool seen_result = false;
  context->getHeaders([this, &seen_result](LookupResult&& result, bool end_stream) {
    lookup_result_ = std::move(result);
    lookup_end_stream_after_headers_ = end_stream;
    seen_result = true;
  });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return context;
}

absl::Status HttpCacheImplementationTest::insert(
    LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& headers,
    const absl::string_view body, const absl::optional<Http::TestResponseTrailerMapImpl> trailers) {
  // For responses with body, we must wait for insertBody's callback before
  // calling insertTrailers or completing. Note, in a multipart body test this
  // would need to check for the callback having been called for *every* body part,
  // but since the test only uses single-part bodies, inserting trailers or
  // completing in direct response to the callback works.
  bool inserted_headers = false;
  bool inserted_body = false;
  bool inserted_trailers = false;
  InsertContextPtr inserter = cache()->makeInsertContext(std::move(lookup), encoder_callbacks_);
  absl::Cleanup destroy_inserter{[&inserter] { inserter->onDestroy(); }};
  const ResponseMetadata metadata{time_system_.systemTime()};

  bool headers_end_stream = body.empty() && !trailers.has_value();
  inserter->insertHeaders(
      headers, metadata, [&inserted_headers](bool result) { inserted_headers = result; },
      headers_end_stream);
  pumpDispatcher();
  if (!inserted_headers) {
    return absl::InternalError("headers were not inserted");
  }
  if (headers_end_stream) {
    return absl::OkStatus();
  }

  if (!body.empty()) {
    inserter->insertBody(
        Buffer::OwnedImpl(body), [&inserted_body](bool result) { inserted_body = result; },
        /*end_stream=*/!trailers.has_value());
    pumpDispatcher();
    if (!inserted_body) {
      return absl::InternalError("body was not inserted");
    }
  }
  if (!trailers.has_value()) {
    return absl::OkStatus();
  }
  inserter->insertTrailers(trailers.value(),
                           [&inserted_trailers](bool result) { inserted_trailers = result; });
  pumpDispatcher();
  if (!inserted_trailers) {
    return absl::InternalError("trailers were not inserted");
  }
  return absl::OkStatus();
}

LookupContextPtr HttpCacheImplementationTest::lookupContextWithAllParts() {
  absl::string_view path = "/common";
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};
  Http::TestResponseTrailerMapImpl response_trailers{
      {"common-trailer", "irrelevant value"},
  };
  EXPECT_THAT(insert(lookup(path), response_headers, "commonbody", response_trailers), IsOk());
  LookupRequest request = makeLookupRequest(path);
  return cache()->makeLookupContext(std::move(request), decoder_callbacks_);
}

absl::Status HttpCacheImplementationTest::insert(absl::string_view request_path,
                                                 const Http::TestResponseHeaderMapImpl& headers,
                                                 const absl::string_view body) {
  return insert(lookup(request_path), headers, body);
}

std::pair<Http::ResponseHeaderMapPtr, bool>
HttpCacheImplementationTest::getHeaders(LookupContext& context) {
  std::pair<Http::ResponseHeaderMapPtr, bool> returned_pair;
  bool seen_result = false;
  context.getHeaders([&returned_pair, &seen_result](LookupResult&& lookup_result, bool end_stream) {
    EXPECT_NE(lookup_result.cache_entry_status_, CacheEntryStatus::Unusable);
    EXPECT_NE(lookup_result.headers_, nullptr);
    returned_pair.first = std::move(lookup_result.headers_);
    returned_pair.second = end_stream;
    seen_result = true;
  });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return returned_pair;
}

std::pair<std::string, bool> HttpCacheImplementationTest::getBody(LookupContext& context,
                                                                  uint64_t start, uint64_t end) {
  AdjustedByteRange range(start, end);
  std::pair<std::string, bool> returned_pair;
  bool seen_result = false;
  context.getBody(range,
                  [&returned_pair, &seen_result](Buffer::InstancePtr&& data, bool end_stream) {
                    EXPECT_NE(data, nullptr);
                    returned_pair = std::make_pair(data->toString(), end_stream);
                    seen_result = true;
                  });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return returned_pair;
}

Http::TestResponseTrailerMapImpl HttpCacheImplementationTest::getTrailers(LookupContext& context) {
  Http::ResponseTrailerMapPtr trailers;
  context.getTrailers([&trailers](Http::ResponseTrailerMapPtr&& data) {
    if (data) {
      trailers = std::move(data);
    }
  });
  pumpDispatcher();
  EXPECT_THAT(trailers, testing::NotNull());
  return *trailers;
}

LookupRequest HttpCacheImplementationTest::makeLookupRequest(absl::string_view request_path) {
  request_headers_.setPath(request_path);
  return {request_headers_, time_system_.systemTime(), vary_allow_list_};
}

testing::AssertionResult HttpCacheImplementationTest::expectLookupSuccessWithHeaders(
    LookupContext* lookup_context, const Http::TestResponseHeaderMapImpl& headers) {
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
  if (!TestUtility::headerMapEqualIgnoreOrder(headers, *lookup_result_.headers_)) {
    return AssertionFailure() << "Expected headers: " << headers
                              << "\nActual:  " << *lookup_result_.headers_;
  }
  return AssertionSuccess();
}

testing::AssertionResult HttpCacheImplementationTest::expectLookupSuccessWithBodyAndTrailers(
    LookupContext* lookup_context, absl::string_view body,
    Http::TestResponseTrailerMapImpl trailers) {
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
  const auto [actual_body, end_stream] = getBody(*lookup_context, 0, body.size());
  if (body != actual_body) {
    return AssertionFailure() << "Expected body == " << body << "\n  Actual:  " << actual_body;
  }
  if (!end_stream) {
    const Http::TestResponseTrailerMapImpl actual_trailers = getTrailers(*lookup_context);
    if (trailers != actual_trailers) {
      return AssertionFailure() << "Expected trailers == " << trailers
                                << "\n  Actual:  " << actual_trailers;
    }
  } else if (!trailers.empty()) {
    return AssertionFailure() << "Expected trailers == " << trailers
                              << "\n  Actual: end_stream after body";
  }
  return AssertionSuccess();
}

// Simple flow of putting in an item, getting it, deleting it.
TEST_P(HttpCacheImplementationTest, PutGet) {
  const std::string request_path1("/name");
  LookupContextPtr name_lookup_context = lookup(request_path1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const std::string body1("Value");
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, body1), IsOk());
  name_lookup_context = lookup(request_path1);
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(name_lookup_context.get(), body1));

  const std::string& request_path2("/another-name");
  LookupContextPtr another_name_lookup_context = lookup(request_path2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string new_body1("NewValue");
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, new_body1), IsOk());
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(lookup(request_path1).get(), new_body1));
}

TEST_P(HttpCacheImplementationTest, PrivateResponse) {
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"age", "2"},
      {"cache-control", "private,max-age=3600"}};
  const std::string request_path("/name");

  LookupContextPtr name_lookup_context = lookup(request_path);
  ASSERT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string body("Value");
  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup.
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, body), IsOk());

  LookupContextPtr next_lookup = lookup(request_path);
  ASSERT_TRUE(expectLookupSuccessWithBodyAndTrailers(next_lookup.get(), body));
  next_lookup->onDestroy();
}

TEST_P(HttpCacheImplementationTest, Miss) {
  LookupContextPtr name_lookup_context = lookup("/name");
  ASSERT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, Fresh) {
  const std::string time_value_1 = formatter_.fromTime(time_system_.systemTime());
  const Http::TestResponseHeaderMapImpl response_headers = {
      {"date", time_value_1}, {"cache-control", "public, max-age=3600"}};
  // TODO(toddmgreer): Test with various date headers.
  ASSERT_THAT(insert("/", response_headers, ""), IsOk());
  time_system_.advanceTimeWait(Seconds(3600));
  lookup("/");
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, StaleUnusable) {
  if (validationEnabled()) {
    // This test is for HttpCache implementations that do not yet support
    // updateHeaders (and instead return Unusable), so skip this test if the
    // delegate enables validation.
    GTEST_SKIP();
  }
  SystemTime insert_time = time_system_.systemTime();
  const Http::TestResponseHeaderMapImpl headers = {{":status", "200"},
                                                   {"date", formatter_.fromTime(insert_time)},
                                                   {"cache-control", "public, max-age=3600"}};
  ASSERT_THAT(insert("/", headers, ""), IsOk());

  time_system_.advanceTimeWait(Seconds(3601));

  LookupContextPtr a_lookup = lookup("/");
  a_lookup->onDestroy();
  ASSERT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, StaleRequiresValidation) {
  if (!validationEnabled()) {
    // Caches that do not implement or disable validation should skip this test.
    GTEST_SKIP();
  }
  SystemTime insert_time = time_system_.systemTime();
  const Http::TestResponseHeaderMapImpl headers = {{":status", "200"},
                                                   {"date", formatter_.fromTime(insert_time)},
                                                   {"etag", "\"foo\""},
                                                   {"cache-control", "public, max-age=3600"}};
  ASSERT_THAT(insert("/", headers, ""), IsOk());

  time_system_.advanceTimeWait(Seconds(3601));

  LookupContextPtr a_lookup = lookup("/");
  a_lookup->onDestroy();
  ASSERT_EQ(CacheEntryStatus::RequiresValidation, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, RequestSmallMinFresh) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "min-fresh=1000");
  const std::string request_path("/name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  ASSERT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  SystemTime insert_time = time_system_.systemTime();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"date", formatter_.fromTime(insert_time)},
                                                   {"age", "6000"},
                                                   {"cache-control", "public, max-age=9000"}};
  const std::string body("Value");
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, body), IsOk());

  LookupContextPtr next_lookup = lookup(request_path);
  ASSERT_TRUE(expectLookupSuccessWithBodyAndTrailers(next_lookup.get(), body));
  next_lookup->onDestroy();
}

TEST_P(HttpCacheImplementationTest, ResponseStaleWithRequestLargeMaxStale) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-stale=9000");

  const std::string request_path("/name");
  LookupContextPtr name_lookup_context = lookup(request_path);
  ASSERT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  SystemTime insert_time = time_system_.systemTime();
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"date", formatter_.fromTime(insert_time)},
                                          {"age", "7200"},
                                          {"cache-control", "public, max-age=3600"}};

  const std::string body("Value");
  ASSERT_THAT(insert(std::move(name_lookup_context), headers, body), IsOk());

  LookupContextPtr next_lookup = lookup(request_path);
  ASSERT_TRUE(expectLookupSuccessWithBodyAndTrailers(next_lookup.get(), body));
  next_lookup->onDestroy();
}

TEST_P(HttpCacheImplementationTest, StreamingPut) {
  SystemTime insert_time = time_system_.systemTime();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"date", formatter_.fromTime(insert_time)},
                                                   {"age", "2"},
                                                   {"cache-control", "public, max-age=3600"}};
  const std::string request_path("/path");
  InsertContextPtr inserter = cache()->makeInsertContext(lookup(request_path), encoder_callbacks_);
  absl::Cleanup destroy_inserter{[&inserter] { inserter->onDestroy(); }};
  ResponseMetadata metadata{time_system_.systemTime()};
  bool inserted_headers = false;
  bool inserted_body1 = false;
  bool inserted_body2 = false;
  inserter->insertHeaders(
      response_headers, metadata, [&inserted_headers](bool ready) { inserted_headers = ready; },
      false);
  pumpDispatcher();
  ASSERT_TRUE(inserted_headers);
  inserter->insertBody(
      Buffer::OwnedImpl("Hello, "), [&inserted_body1](bool ready) { inserted_body1 = ready; },
      false);
  pumpDispatcher();
  ASSERT_TRUE(inserted_body1);
  inserter->insertBody(
      Buffer::OwnedImpl("World!"), [&inserted_body2](bool ready) { inserted_body2 = ready; }, true);
  pumpDispatcher();
  ASSERT_TRUE(inserted_body2);

  LookupContextPtr name_lookup = lookup(request_path);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  ASSERT_NE(nullptr, lookup_result_.headers_);
  ASSERT_EQ(13, lookup_result_.content_length_);
  ASSERT_THAT(getBody(*name_lookup, 0, 13), testing::Pair("Hello, World!", true));
  name_lookup->onDestroy();
}

TEST_P(HttpCacheImplementationTest, VaryResponses) {
  // Responses will vary on accept.
  const std::string request_path("/path");
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"vary", "accept"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"why", "is"}, {"sky", "blue"}};

  // First request.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "image/*");

  LookupContextPtr first_lookup_miss = lookup(request_path);
  EXPECT_EQ(lookup_result_.cache_entry_status_, CacheEntryStatus::Unusable);
  const std::string body1("accept is image/*");
  ASSERT_THAT(insert(std::move(first_lookup_miss), response_headers, body1, response_trailers),
              IsOk());
  LookupContextPtr first_lookup_hit = lookup(request_path);
  EXPECT_TRUE(
      expectLookupSuccessWithBodyAndTrailers(first_lookup_hit.get(), body1, response_trailers));

  // Second request with a different value for the varied header.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "text/html");
  LookupContextPtr second_lookup_miss = lookup(request_path);
  // Should miss because we don't have this version of the response saved yet.
  EXPECT_EQ(lookup_result_.cache_entry_status_, CacheEntryStatus::Unusable);
  // Add second version and make sure we receive the correct one.
  const std::string body2("accept is text/html");
  ASSERT_THAT(insert(std::move(second_lookup_miss), response_headers, body2), IsOk());

  LookupContextPtr second_lookup_hit = lookup(request_path);
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(second_lookup_hit.get(), body2));

  // Set the headers for the first request again.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "image/*");
  time_system_.advanceTimeWait(Seconds(1));

  LookupContextPtr first_lookup_hit_again = lookup(request_path);
  // Looks up first version again to be sure it wasn't replaced with the second
  // one.
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(first_lookup_hit_again.get(), body1,
                                                     response_trailers));

  // Create a new allow list to make sure a now disallowed cached vary entry is not served.
  Protobuf::RepeatedPtrField<::envoy::type::matcher::v3::StringMatcher> proto_allow_list;
  ::envoy::type::matcher::v3::StringMatcher* matcher = proto_allow_list.Add();
  matcher->set_exact("width");
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  vary_allow_list_ = VaryAllowList(proto_allow_list, factory_context);
  lookup(request_path);
  EXPECT_EQ(lookup_result_.cache_entry_status_, CacheEntryStatus::Unusable);
}

TEST_P(HttpCacheImplementationTest, VaryOnDisallowedKey) {
  // Responses will vary on accept.
  const std::string request_path("/path");
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"vary", "user-agent"}};

  // First request.
  request_headers_.setCopy(Http::LowerCaseString("user-agent"), "user_agent_one");
  LookupContextPtr first_value_vary = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
  const std::string body("one");
  ASSERT_THAT(insert(std::move(first_value_vary), response_headers, body), Not(IsOk()));
  first_value_vary = lookup(request_path);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersAndMetadata) {
  if (!validationEnabled()) {
    // Caches that do not implement or disable validation should skip this test.
    GTEST_SKIP();
  }

  const std::string request_path_1("/name");

  {
    Http::TestResponseHeaderMapImpl response_headers{
        {"date", formatter_.fromTime(time_system_.systemTime())},
        {"cache-control", "public,max-age=3600"},
        {":status", "200"},
        {"etag", "\"foo\""},
        {"content-length", "4"}};

    ASSERT_THAT(insert(request_path_1, response_headers, "body"), IsOk());
    auto lookup_context = lookup(request_path_1);
    lookup_context->onDestroy();
    ASSERT_NE(lookup_result_.headers_, nullptr);

    // An age header is inserted by `makeLookupResult`
    response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
    EXPECT_THAT(lookup_result_.headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers));
  }

  // Update the date field in the headers
  time_system_.advanceTimeWait(Seconds(3601));

  {
    Http::TestResponseHeaderMapImpl response_headers =
        Http::TestResponseHeaderMapImpl{{"date", formatter_.fromTime(time_system_.systemTime())},
                                        {"cache-control", "public,max-age=3600"},
                                        {":status", "200"},
                                        {"etag", "\"foo\""},
                                        {"content-length", "4"}};
    EXPECT_TRUE(updateHeaders(request_path_1, response_headers, {time_system_.systemTime()}));
    auto lookup_context = lookup(request_path_1);
    lookup_context->onDestroy();

    // An age header is inserted by `makeLookupResult`
    response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
    EXPECT_THAT(lookup_result_.headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers));
  }
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersForMissingKeyFails) {
  const std::string request_path_1("/name");
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"etag", "\"foo\""},
  };
  time_system_.advanceTimeWait(Seconds(3601));
  EXPECT_FALSE(updateHeaders(request_path_1, response_headers, {time_system_.systemTime()}));
  lookup(request_path_1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersForVaryHeaders) {
  if (!validationEnabled()) {
    // UpdateHeaders would not be called when validation is disabled.
    GTEST_SKIP();
  }

  const std::string request_path_1("/name");
  const std::string time_value_1 = formatter_.fromTime(time_system_.systemTime());
  Http::TestResponseHeaderMapImpl response_headers_1{{":status", "200"},
                                                     {"date", time_value_1},
                                                     {"cache-control", "public,max-age=3600"},
                                                     {"accept", "image/*"},
                                                     {"vary", "accept"}};
  ASSERT_THAT(insert(request_path_1, response_headers_1, "body"), IsOk());
  // An age header is inserted by `makeLookUpResult`
  response_headers_1.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_TRUE(expectLookupSuccessWithHeaders(lookup(request_path_1).get(), response_headers_1));
  // Update the date field in the headers
  time_system_.advanceTimeWait(Seconds(3600));
  const SystemTime time_2 = time_system_.systemTime();
  const std::string time_value_2 = formatter_.fromTime(time_2);
  Http::TestResponseHeaderMapImpl response_headers_2{{":status", "200"},
                                                     {"date", time_value_2},
                                                     {"cache-control", "public,max-age=3600"},
                                                     {"accept", "image/*"},
                                                     {"vary", "accept"}};
  EXPECT_TRUE(updateHeaders(request_path_1, response_headers_2, {time_2}));
  response_headers_2.setReferenceKey(Http::LowerCaseString("age"), "0");
  // the age is still 0 because an entry is considered fresh after validation
  EXPECT_TRUE(expectLookupSuccessWithHeaders(lookup(request_path_1).get(), response_headers_2));
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersSkipEtagHeader) {
  if (!validationEnabled()) {
    // UpdateHeaders is not called when validation is disabled.
    GTEST_SKIP();
  }

  const std::string request_path_1("/name");
  const std::string time_value_1 = formatter_.fromTime(time_system_.systemTime());
  Http::TestResponseHeaderMapImpl response_headers_1{{":status", "200"},
                                                     {"date", time_value_1},
                                                     {"cache-control", "public,max-age=3600"},
                                                     {"etag", "0000-0000"}};
  ASSERT_THAT(insert(request_path_1, response_headers_1, "body"), IsOk());
  // An age header is inserted by `makeLookUpResult`
  response_headers_1.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_TRUE(expectLookupSuccessWithHeaders(lookup(request_path_1).get(), response_headers_1));

  // Update the date field in the headers
  time_system_.advanceTimeWait(Seconds(3601));
  const SystemTime time_2 = time_system_.systemTime();
  const std::string time_value_2 = formatter_.fromTime(time_2);
  Http::TestResponseHeaderMapImpl response_headers_2{{":status", "200"},
                                                     {"date", time_value_2},
                                                     {"cache-control", "public,max-age=3600"},
                                                     {"etag", "1111-1111"}};
  // The etag header should not be updated
  Http::TestResponseHeaderMapImpl response_headers_3{{":status", "200"},
                                                     {"date", time_value_2},
                                                     {"cache-control", "public,max-age=3600"},
                                                     {"etag", "0000-0000"}};

  EXPECT_TRUE(updateHeaders(request_path_1, response_headers_2, {time_2}));
  response_headers_3.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_TRUE(expectLookupSuccessWithHeaders(lookup(request_path_1).get(), response_headers_3));
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersSkipSpecificHeaders) {
  if (!validationEnabled()) {
    // UpdateHeaders is not called when validation is disabled.
    GTEST_SKIP();
  }

  const std::string request_path_1("/name");
  const std::string time_value_1 = formatter_.fromTime(time_system_.systemTime());

  // Vary not tested because we have separate tests that cover it
  Http::TestResponseHeaderMapImpl origin_response_headers{
      {":status", "200"},
      {"date", time_value_1},
      {"cache-control", "public,max-age=3600"},
      {"content-range", "bytes 200-1000/67589"},
      {"content-length", "800"},
      {"etag", "0000-0000"},
      {"etag", "1111-1111"},
      {"link", "<https://example.com>; rel=\"preconnect\""}};
  ASSERT_THAT(insert(request_path_1, origin_response_headers, "body"), IsOk());

  // An age header is inserted by `makeLookUpResult`
  origin_response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_TRUE(
      expectLookupSuccessWithHeaders(lookup(request_path_1).get(), origin_response_headers));
  time_system_.advanceTimeWait(Seconds(100));

  const SystemTime time_2 = time_system_.systemTime();
  const std::string time_value_2 = formatter_.fromTime(time_2);
  Http::TestResponseHeaderMapImpl incoming_response_headers{
      {":status", "200"},
      {"date", time_value_2},
      {"cache-control", "public,max-age=3600"},
      {"content-range", "bytes 5-1000/67589"},
      {"content-length", "995"},
      {"content-length", "996"},
      {"age", "20"},
      {"etag", "2222-2222"},
      {"link", "<https://changed.com>; rel=\"preconnect\""}};

  // The skipped headers should not be updated
  // "age" and "link" should be updated
  Http::TestResponseHeaderMapImpl expected_response_headers{
      {":status", "200"},
      {"date", time_value_2},
      {"cache-control", "public,max-age=3600"},
      {"content-range", "bytes 200-1000/67589"},
      {"content-length", "800"},
      {"age", "20"},
      {"etag", "0000-0000"},
      {"etag", "1111-1111"},
      {"link", "<https://changed.com>; rel=\"preconnect\""}};

  EXPECT_TRUE(updateHeaders(request_path_1, incoming_response_headers, {time_2}));
  EXPECT_TRUE(
      expectLookupSuccessWithHeaders(lookup(request_path_1).get(), expected_response_headers));
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersWithMultivalue) {
  if (!validationEnabled()) {
    // UpdateHeaders is not called when validation is disabled.
    GTEST_SKIP();
  }

  const std::string request_path_1("/name");

  const SystemTime time_1 = time_system_.systemTime();
  const std::string time_value_1(formatter_.fromTime(time_1));
  // Vary not tested because we have separate tests that cover it
  Http::TestResponseHeaderMapImpl response_headers_1{
      {":status", "200"},
      {"date", time_value_1},
      {"cache-control", "public,max-age=3600"},
      {"etag", "\"foo\""},
      {"link", "<https://www.example.com>; rel=\"preconnect\""},
      {"link", "<https://example.com>; rel=\"preconnect\""}};
  ASSERT_THAT(insert(request_path_1, response_headers_1, "body"), IsOk());
  lookup(request_path_1);
  response_headers_1.setCopy(Http::LowerCaseString("age"), "0");
  EXPECT_THAT(lookup_result_.headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers_1));

  time_system_.advanceTimeWait(Seconds(3601));
  const SystemTime time_2 = time_system_.systemTime();
  const std::string time_value_2 = formatter_.fromTime(time_2);

  Http::TestResponseHeaderMapImpl response_headers_2{
      {":status", "200"},
      {"date", time_value_2},
      {"cache-control", "public,max-age=3600"},
      {"etag", "\"foo\""},
      {"link", "<https://www.another-example.com>; rel=\"preconnect\""},
      {"link", "<https://another-example.com>; rel=\"preconnect\""}};

  EXPECT_TRUE(updateHeaders(request_path_1, response_headers_2, {time_2}));
  lookup(request_path_1);
  response_headers_2.setCopy(Http::LowerCaseString("age"), "0");
  EXPECT_THAT(lookup_result_.headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers_2));
}

TEST_P(HttpCacheImplementationTest, PutGetWithTrailers) {
  const std::string request_path1("/name");
  LookupContextPtr name_lookup_context = lookup(request_path1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};
  const std::string body1("Value");
  Http::TestResponseTrailerMapImpl response_trailers{{"why", "is"}, {"sky", "blue"}};
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, body1, response_trailers),
              IsOk());
  name_lookup_context = lookup(request_path1);
  EXPECT_TRUE(
      expectLookupSuccessWithBodyAndTrailers(name_lookup_context.get(), body1, response_trailers));

  const std::string& request_path2("/another-name");
  LookupContextPtr another_name_lookup_context = lookup(request_path2);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  const std::string new_body1("NewValue");
  ASSERT_THAT(
      insert(std::move(name_lookup_context), response_headers, new_body1, response_trailers),
      IsOk());
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(lookup(request_path1).get(), new_body1,
                                                     response_trailers));
}

TEST_P(HttpCacheImplementationTest, EmptyTrailers) {
  const std::string request_path1("/name");
  LookupContextPtr name_lookup_context = lookup(request_path1);
  EXPECT_EQ(CacheEntryStatus::Unusable, lookup_result_.cache_entry_status_);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};
  const std::string body1("Value");
  ASSERT_THAT(insert(std::move(name_lookup_context), response_headers, body1), IsOk());
  name_lookup_context = lookup(request_path1);
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(name_lookup_context.get(), body1));
}

TEST_P(HttpCacheImplementationTest, DoesNotRunHeadersCallbackWhenCancelledAfterPosted) {
  bool was_called = false;
  {
    LookupContextPtr context = lookupContextWithAllParts();
    context->getHeaders([&was_called](LookupResult&&, bool) { was_called = true; });
    pumpIntoDispatcher();
    context->onDestroy();
  }
  pumpDispatcher();
  EXPECT_FALSE(was_called);
}

TEST_P(HttpCacheImplementationTest, DoesNotRunBodyCallbackWhenCancelledAfterPosted) {
  bool was_called = false;
  {
    LookupContextPtr context = lookupContextWithAllParts();
    context->getHeaders([](LookupResult&&, bool) {});
    pumpDispatcher();
    context->getBody({0, 10}, [&was_called](Buffer::InstancePtr&&, bool) { was_called = true; });
    pumpIntoDispatcher();
    context->onDestroy();
  }
  pumpDispatcher();
  EXPECT_FALSE(was_called);
}

TEST_P(HttpCacheImplementationTest, DoesNotRunTrailersCallbackWhenCancelledAfterPosted) {
  bool was_called = false;
  {
    LookupContextPtr context = lookupContextWithAllParts();
    context->getHeaders([](LookupResult&&, bool) {});
    pumpDispatcher();
    context->getBody({0, 10}, [](Buffer::InstancePtr&&, bool) {});
    pumpDispatcher();
    context->getTrailers([&was_called](Http::ResponseTrailerMapPtr&&) { was_called = true; });
    pumpIntoDispatcher();
    context->onDestroy();
  }
  pumpDispatcher();
  EXPECT_FALSE(was_called);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
