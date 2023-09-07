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

HttpCacheImplementationTest::HttpCacheImplementationTest()
    : delegate_(GetParam()()), vary_allow_list_(getConfig().allowed_vary_headers()) {
  request_headers_.setMethod("GET");
  request_headers_.setHost("example.com");
  request_headers_.setScheme("https");
  request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");

  EXPECT_CALL(dispatcher_, post(_)).Times(AnyNumber());
  EXPECT_CALL(dispatcher_, isThreadSafe()).Times(AnyNumber());

  delegate_->setUp(dispatcher_);
}

HttpCacheImplementationTest::~HttpCacheImplementationTest() {
  Assert::resetEnvoyBugCountersForTest();

  delegate_->tearDown();
}

bool HttpCacheImplementationTest::updateHeaders(
    absl::string_view request_path, const Http::TestResponseHeaderMapImpl& response_headers,
    const ResponseMetadata& metadata) {
  LookupContextPtr lookup_context = lookup(request_path);
  auto update_promise = std::make_shared<std::promise<bool>>();
  cache()->updateHeaders(*lookup_context, response_headers, metadata,
                         [update_promise](bool result) { update_promise->set_value(result); });
  auto update_future = update_promise->get_future();
  if (std::future_status::ready != update_future.wait_for(std::chrono::seconds(5))) {
    EXPECT_TRUE(false) << "timed out in updateHeaders " << request_path;
    return false;
  }
  return update_future.get();
}

LookupContextPtr HttpCacheImplementationTest::lookup(absl::string_view request_path) {
  LookupRequest request = makeLookupRequest(request_path);
  LookupContextPtr context = cache()->makeLookupContext(std::move(request), decoder_callbacks_);
  auto headers_promise = std::make_shared<std::promise<LookupResult>>();
  context->getHeaders(
      [headers_promise](LookupResult&& result) { headers_promise->set_value(std::move(result)); });
  auto headers_future = headers_promise->get_future();
  if (std::future_status::ready == headers_future.wait_for(std::chrono::seconds(5))) {
    lookup_result_ = headers_future.get();
  } else {
    EXPECT_TRUE(false) << "timed out in lookup " << request_path;
  }

  return context;
}

absl::Status HttpCacheImplementationTest::insert(LookupContextPtr lookup,
                                                 const Http::TestResponseHeaderMapImpl& headers,
                                                 const absl::string_view body,
                                                 std::chrono::milliseconds timeout) {
  return insert(std::move(lookup), headers, body, absl::nullopt, timeout);
}

absl::Status HttpCacheImplementationTest::insert(
    LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& headers,
    const absl::string_view body, const absl::optional<Http::TestResponseTrailerMapImpl> trailers,
    std::chrono::milliseconds timeout) {
  // For responses with body, we must wait for insertBody's callback before
  // calling insertTrailers or completing. Note, in a multipart body test this
  // would need to check for the callback having been called for *every* body part,
  // but since the test only uses single-part bodies, inserting trailers or
  // completing in direct response to the callback works.
  std::shared_ptr<std::promise<bool>> insert_promise;
  auto make_insert_callback = [&insert_promise]() {
    insert_promise = std::make_shared<std::promise<bool>>();
    return [insert_promise](bool success_ready_for_more) {
      insert_promise->set_value(success_ready_for_more);
    };
  };
  auto wait_for_insert = [&insert_promise, timeout](absl::string_view fn) {
    auto future = insert_promise->get_future();
    auto result = future.wait_for(timeout);
    if (result == std::future_status::timeout) {
      return absl::DeadlineExceededError(absl::StrCat("Timed out waiting for ", fn));
    }
    if (!future.get()) {
      return absl::UnknownError(absl::StrCat("Insert was aborted by cache in ", fn));
    }
    return absl::OkStatus();
  };

  InsertContextPtr inserter = cache()->makeInsertContext(std::move(lookup), encoder_callbacks_);
  absl::Cleanup destroy_inserter{[&inserter] { inserter->onDestroy(); }};
  const ResponseMetadata metadata{time_system_.systemTime()};

  bool headers_end_stream = body.empty() && !trailers.has_value();
  inserter->insertHeaders(headers, metadata, make_insert_callback(), headers_end_stream);
  auto status = wait_for_insert("insertHeaders()");
  if (!status.ok()) {
    return status;
  }
  if (headers_end_stream) {
    return absl::OkStatus();
  }

  if (!body.empty()) {
    inserter->insertBody(Buffer::OwnedImpl(body), make_insert_callback(),
                         /*end_stream=*/!trailers.has_value());
    auto status = wait_for_insert("insertBody()");
    if (!status.ok()) {
      return status;
    }
  }

  if (trailers.has_value()) {
    inserter->insertTrailers(trailers.value(), make_insert_callback());
    auto status = wait_for_insert("insertTrailers()");
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status HttpCacheImplementationTest::insert(absl::string_view request_path,
                                                 const Http::TestResponseHeaderMapImpl& headers,
                                                 const absl::string_view body,
                                                 std::chrono::milliseconds timeout) {
  return insert(lookup(request_path), headers, body, timeout);
}

Http::ResponseHeaderMapPtr HttpCacheImplementationTest::getHeaders(LookupContext& context) {
  Http::ResponseHeaderMapPtr response_headers_ptr;
  auto headers_promise = std::make_shared<std::promise<Http::ResponseHeaderMapPtr>>();
  context.getHeaders([headers_promise](LookupResult&& lookup_result) {
    EXPECT_NE(lookup_result.cache_entry_status_, CacheEntryStatus::Unusable);
    EXPECT_NE(lookup_result.headers_, nullptr);
    headers_promise->set_value(std::move(lookup_result.headers_));
  });
  auto future = headers_promise->get_future();
  EXPECT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(5)));
  return future.get();
}

std::string HttpCacheImplementationTest::getBody(LookupContext& context, uint64_t start,
                                                 uint64_t end) {
  AdjustedByteRange range(start, end);
  auto body_promise = std::make_shared<std::promise<std::string>>();
  context.getBody(range, [body_promise](Buffer::InstancePtr&& data) {
    EXPECT_NE(data, nullptr);
    body_promise->set_value(data ? data->toString() : "");
  });
  auto future = body_promise->get_future();
  EXPECT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(5)));
  return future.get();
}

Http::TestResponseTrailerMapImpl HttpCacheImplementationTest::getTrailers(LookupContext& context) {
  auto trailers_promise =
      std::make_shared<std::promise<std::shared_ptr<Http::ResponseTrailerMap>>>();
  context.getTrailers([trailers_promise](Http::ResponseTrailerMapPtr&& data) {
    if (data) {
      trailers_promise->set_value(std::move(data));
    }
  });
  auto future = trailers_promise->get_future();
  EXPECT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(5)));
  Http::TestResponseTrailerMapImpl trailers;
  if (std::future_status::ready == future.wait_for(std::chrono::seconds(5))) {
    trailers = *future.get();
  }
  return trailers;
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
  const std::string actual_body = getBody(*lookup_context, 0, body.size());
  if (body != actual_body) {
    return AssertionFailure() << "Expected body == " << body << "\n  Actual:  " << actual_body;
  }
  if (lookup_result_.has_trailers_) {
    const Http::TestResponseTrailerMapImpl actual_trailers = getTrailers(*lookup_context);
    if (trailers != actual_trailers) {
      return AssertionFailure() << "Expected trailers == " << trailers
                                << "\n  Actual:  " << actual_trailers;
    }
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
  std::promise<bool> insert_headers_promise;
  std::promise<bool> insert_body1_promise;
  std::promise<bool> insert_body2_promise;
  inserter->insertHeaders(
      response_headers, metadata,
      [&insert_headers_promise](bool ready) { insert_headers_promise.set_value(ready); }, false);
  auto insert_future = insert_headers_promise.get_future();
  ASSERT_EQ(std::future_status::ready, insert_future.wait_for(std::chrono::seconds(5)))
      << "timed out waiting for inserts to complete";
  ASSERT_TRUE(insert_future.get());
  inserter->insertBody(
      Buffer::OwnedImpl("Hello, "),
      [&insert_body1_promise](bool ready) { insert_body1_promise.set_value(ready); }, false);
  insert_future = insert_body1_promise.get_future();
  ASSERT_EQ(std::future_status::ready, insert_future.wait_for(std::chrono::seconds(5)))
      << "timed out waiting for inserts to complete";
  ASSERT_TRUE(insert_future.get());
  inserter->insertBody(
      Buffer::OwnedImpl("World!"),
      [&insert_body2_promise](bool ready) { insert_body2_promise.set_value(ready); }, true);
  insert_future = insert_body2_promise.get_future();
  ASSERT_EQ(std::future_status::ready, insert_future.wait_for(std::chrono::seconds(5)))
      << "timed out waiting for inserts to complete";
  ASSERT_TRUE(insert_future.get());

  LookupContextPtr name_lookup = lookup(request_path);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_result_.cache_entry_status_);
  ASSERT_NE(nullptr, lookup_result_.headers_);
  ASSERT_EQ(13, lookup_result_.content_length_);
  ASSERT_EQ("Hello, World!", getBody(*name_lookup, 0, 13));
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

  // First request.
  request_headers_.setCopy(Http::LowerCaseString("accept"), "image/*");

  LookupContextPtr first_lookup_miss = lookup(request_path);
  EXPECT_EQ(lookup_result_.cache_entry_status_, CacheEntryStatus::Unusable);
  const std::string body1("accept is image/*");
  ASSERT_THAT(insert(std::move(first_lookup_miss), response_headers, body1), IsOk());
  LookupContextPtr first_lookup_hit = lookup(request_path);
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(first_lookup_hit.get(), body1));

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
  EXPECT_TRUE(expectLookupSuccessWithBodyAndTrailers(first_lookup_hit_again.get(), body1));

  // Create a new allow list to make sure a now disallowed cached vary entry is not served.
  Protobuf::RepeatedPtrField<::envoy::type::matcher::v3::StringMatcher> proto_allow_list;
  ::envoy::type::matcher::v3::StringMatcher* matcher = proto_allow_list.Add();
  matcher->set_exact("width");
  vary_allow_list_ = VaryAllowList(proto_allow_list);
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
  EXPECT_TRUE(lookup_result_.has_trailers_);
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
  EXPECT_FALSE(lookup_result_.has_trailers_);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
