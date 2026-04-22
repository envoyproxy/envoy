#include "test/extensions/filters/http/cache_v2/http_cache_implementation_test_common.h"

#include <future>
#include <string>
#include <utility>

#include "source/common/common/assert.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"

#include "test/extensions/filters/http/cache_v2/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"

using ::envoy::extensions::filters::http::cache_v2::v3::CacheV2Config;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Mock;
using ::testing::MockFunction;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Property;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

inline constexpr auto RangeIs = [](const auto& m1, const auto& m2) {
  return AllOf(Property("begin", &AdjustedByteRange::begin, m1),
               Property("end", &AdjustedByteRange::end, m2));
};

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

HttpCacheImplementationTest::HttpCacheImplementationTest() : delegate_(GetParam()()) {
  request_headers_.setMethod("GET");
  request_headers_.setHost("example.com");
  request_headers_.setScheme("https");
  request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
  delegate_->setUp();
}

HttpCacheImplementationTest::~HttpCacheImplementationTest() {
  Assert::resetEnvoyBugCountersForTest();

  delegate_->tearDown();
}

void HttpCacheImplementationTest::updateHeaders(
    absl::string_view request_path, const Http::TestResponseHeaderMapImpl& response_headers,
    const ResponseMetadata& metadata) {
  Key key = simpleKey(request_path);
  cache().updateHeaders(dispatcher(), key, response_headers, metadata);
  pumpDispatcher();
}

LookupResult HttpCacheImplementationTest::lookup(absl::string_view request_path) {
  LookupRequest request = makeLookupRequest(request_path);
  LookupResult result;
  bool seen_result = false;
  cache().lookup(std::move(request), [&result, &seen_result](absl::StatusOr<LookupResult>&& r) {
    result = std::move(r.value());
    seen_result = true;
  });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return result;
}

CacheReaderPtr HttpCacheImplementationTest::insert(
    Key key, const Http::TestResponseHeaderMapImpl& headers, const absl::string_view body,
    const absl::optional<Http::TestResponseTrailerMapImpl> trailers) {
  // For responses with body, we must wait for insertBody's callback before
  // calling insertTrailers or completing. Note, in a multipart body test this
  // would need to check for the callback having been called for *every* body part,
  // but since the test only uses single-part bodies, inserting trailers or
  // completing in direct response to the callback works.
  uint64_t body_insert_pos = 0;
  bool last_body_end_stream = false;
  std::unique_ptr<FakeStreamHttpSource> source;
  bool end_stream_after_headers = body.empty() && !trailers;
  if (!end_stream_after_headers) {
    source = std::make_unique<FakeStreamHttpSource>(
        dispatcher(), nullptr, body,
        trailers ? Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers) : nullptr);
  }
  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  CacheReaderPtr cache_reader;
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&headers), end_stream_after_headers))
      .WillOnce([&cache_reader](CacheReaderPtr cr, Http::ResponseHeaderMapPtr, bool) {
        cache_reader = std::move(cr);
      });
  cache().insert(dispatcher(), key, Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers),
                 metadata, std::move(source), mock_progress_receiver);
  if (!end_stream_after_headers) {
    EXPECT_CALL(*mock_progress_receiver, onBodyInserted)
        .WillRepeatedly([&](AdjustedByteRange range, bool end_stream) {
          EXPECT_THAT(range.begin(), Eq(body_insert_pos));
          body_insert_pos = range.end();
          EXPECT_FALSE(last_body_end_stream);
          last_body_end_stream = end_stream;
        });
  }
  if (trailers) {
    EXPECT_CALL(*mock_progress_receiver, onTrailersInserted(HeaderMapEqualIgnoreOrder(trailers)));
  }
  pumpDispatcher();
  if (!end_stream_after_headers) {
    EXPECT_THAT(body_insert_pos, Eq(body.size()));
    EXPECT_THAT(last_body_end_stream, Eq(trailers ? false : true));
  }
  return cache_reader;
}

CacheReaderPtr HttpCacheImplementationTest::insert(
    absl::string_view request_path, const Http::TestResponseHeaderMapImpl& headers,
    const absl::string_view body, const absl::optional<Http::TestResponseTrailerMapImpl> trailers) {
  return insert(simpleKey(request_path), headers, body, trailers);
}

std::pair<std::string, EndStream>
HttpCacheImplementationTest::getBody(CacheReader& reader, uint64_t start, uint64_t end) {
  AdjustedByteRange range(start, end);
  std::pair<std::string, EndStream> returned_pair;
  bool seen_result = false;
  reader.getBody(dispatcher(), range,
                 [&returned_pair, &seen_result](Buffer::InstancePtr data, EndStream end_stream) {
                   returned_pair = std::make_pair(data->toString(), end_stream);
                   seen_result = true;
                 });
  pumpDispatcher();
  EXPECT_TRUE(seen_result);
  return returned_pair;
}

Key HttpCacheImplementationTest::simpleKey(absl::string_view request_path) const {
  Key key;
  key.set_path(request_path);
  return key;
}

LookupRequest HttpCacheImplementationTest::makeLookupRequest(absl::string_view request_path) const {
  return {simpleKey(request_path), dispatcher()};
}

// Simple flow of putting in an item, getting it, deleting it.
TEST_P(HttpCacheImplementationTest, PutGet) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const std::string body1("Value");
  insert(request_path1, response_headers, body1);
  lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Optional(5));
  EXPECT_THAT(lookup_result.response_headers_, HeaderMapEqualIgnoreOrder(&response_headers));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 5), Pair("Value", EndStream::More));

  const std::string& request_path_2("/another-name");
  LookupResult another_name_lookup_result = lookup(request_path_2);
  EXPECT_THAT(another_name_lookup_result.body_length_, Eq(absl::nullopt));

  const std::string new_body1("NewValue");
  insert(request_path_2, response_headers, new_body1);
  lookup_result = lookup(request_path_2);
  EXPECT_THAT(lookup_result.body_length_, Optional(8));
  EXPECT_THAT(lookup_result.response_headers_, HeaderMapEqualIgnoreOrder(&response_headers));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 8), Pair("NewValue", EndStream::More));
  // Also check that reading chunks of body from arbitrary positions works.
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 4), Pair("NewV", EndStream::More));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 3, 8), Pair("Value", EndStream::More));
}

TEST_P(HttpCacheImplementationTest, UpdateHeadersAndMetadata) {
  const std::string request_path_1("/name");

  {
    Http::TestResponseHeaderMapImpl response_headers{
        {"date", formatter_.fromTime(time_system_.systemTime())},
        {"cache-control", "public,max-age=3600"},
        {":status", "200"},
        {"etag", "\"foo\""},
        {"content-length", "4"}};

    insert(request_path_1, response_headers, "body");
    LookupResult lookup_result = lookup(request_path_1);
    EXPECT_THAT(lookup_result.body_length_, Optional(4));
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
    updateHeaders(request_path_1, response_headers, {time_system_.systemTime()});
    LookupResult lookup_result = lookup(request_path_1);

    EXPECT_THAT(lookup_result.response_headers_.get(),
                HeaderMapEqualIgnoreOrder(&response_headers));
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
  updateHeaders(request_path_1, response_headers, {time_system_.systemTime()});
  LookupResult lookup_result = lookup(request_path_1);
  EXPECT_FALSE(lookup_result.body_length_.has_value());
}

TEST_P(HttpCacheImplementationTest, PutGetWithTrailers) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  Http::TestResponseTrailerMapImpl response_trailers{{"x-trailer1", "hello"},
                                                     {"x-trailer2", "world"}};

  const std::string body1("Value");
  insert(request_path1, response_headers, body1, response_trailers);
  lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Optional(5));
  EXPECT_THAT(lookup_result.response_headers_, HeaderMapEqualIgnoreOrder(&response_headers));
  EXPECT_THAT(lookup_result.response_trailers_, HeaderMapEqualIgnoreOrder(&response_trailers));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 5), Pair("Value", EndStream::More));

  const std::string& request_path_2("/another-name");
  LookupResult another_name_lookup_result = lookup(request_path_2);
  EXPECT_THAT(another_name_lookup_result.body_length_, Eq(absl::nullopt));

  const std::string new_body1("NewValue");
  insert(request_path_2, response_headers, new_body1, response_trailers);
  lookup_result = lookup(request_path_2);
  EXPECT_THAT(lookup_result.body_length_, Optional(8));
  EXPECT_THAT(lookup_result.response_headers_, HeaderMapEqualIgnoreOrder(&response_headers));
  EXPECT_THAT(lookup_result.response_trailers_, HeaderMapEqualIgnoreOrder(&response_trailers));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 8), Pair("NewValue", EndStream::More));
  // Also check that reading chunks of body from arbitrary positions works.
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 0, 4), Pair("NewV", EndStream::More));
  EXPECT_THAT(getBody(*lookup_result.cache_reader_, 3, 8), Pair("Value", EndStream::More));
}

TEST_P(HttpCacheImplementationTest, InsertReadingNullBufferBodyWithEndStream) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};
  const std::string body("Hello World");
  auto source = std::make_unique<MockHttpSource>();
  GetBodyCallback get_body_1, get_body_2;
  EXPECT_CALL(*source, getBody(RangeIs(0, Ge(11)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_1 = std::move(cb); });
  EXPECT_CALL(*source, getBody(RangeIs(11, Ge(11)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_2 = std::move(cb); });
  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  CacheReaderPtr cache_reader;
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&response_headers), false))
      .WillOnce([&cache_reader](CacheReaderPtr cr, Http::ResponseHeaderMapPtr, bool) {
        cache_reader = std::move(cr);
      });
  cache().insert(dispatcher(), simpleKey(request_path1),
                 Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers), metadata,
                 std::move(source), mock_progress_receiver);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(cache_reader, NotNull());
  ASSERT_THAT(get_body_1, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onBodyInserted(RangeIs(0, 11), false));
  get_body_1(std::make_unique<Buffer::OwnedImpl>("Hello World"), EndStream::More);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(get_body_2, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onBodyInserted(RangeIs(0, 11), true));
  get_body_2(nullptr, EndStream::End);
  pumpDispatcher();
}

TEST_P(HttpCacheImplementationTest, HeadersOnlyInsert) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&response_headers), true));
  // source=nullptr indicates that the response was headers-only.
  cache().insert(dispatcher(), simpleKey(request_path1),
                 Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers), metadata,
                 nullptr, mock_progress_receiver);
  pumpDispatcher();
}

TEST_P(HttpCacheImplementationTest, ReadingFromBodyDuringInsert) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const std::string body("Hello World");
  auto source = std::make_unique<MockHttpSource>();
  GetBodyCallback get_body_1, get_body_2;
  EXPECT_CALL(*source, getBody(RangeIs(0, Ge(6)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_1 = std::move(cb); });
  EXPECT_CALL(*source, getBody(RangeIs(6, Ge(11)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_2 = std::move(cb); });
  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  CacheReaderPtr cache_reader;
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&response_headers), false))
      .WillOnce([&cache_reader](CacheReaderPtr cr, Http::ResponseHeaderMapPtr, bool) {
        cache_reader = std::move(cr);
      });
  cache().insert(dispatcher(), simpleKey(request_path1),
                 Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers), metadata,
                 std::move(source), mock_progress_receiver);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(cache_reader, NotNull());
  ASSERT_THAT(get_body_1, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onBodyInserted(RangeIs(0, 6), false));
  get_body_1(std::make_unique<Buffer::OwnedImpl>("Hello "), EndStream::More);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  MockFunction<void(Buffer::InstancePtr, EndStream)> mock_body_callback;
  EXPECT_CALL(mock_body_callback, Call(Pointee(BufferStringEqual("Hello ")), EndStream::More));
  cache_reader->getBody(dispatcher(), AdjustedByteRange(0, 6), mock_body_callback.AsStdFunction());
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(&mock_body_callback);
  ASSERT_THAT(get_body_2, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onBodyInserted(RangeIs(6, 11), true));
  get_body_2(std::make_unique<Buffer::OwnedImpl>("World"), EndStream::End);
  pumpDispatcher();
  EXPECT_CALL(mock_body_callback, Call(Pointee(BufferStringEqual("Hello World")), EndStream::More));
  cache_reader->getBody(dispatcher(), AdjustedByteRange(0, 11), mock_body_callback.AsStdFunction());
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(&mock_body_callback);
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
}

TEST_P(HttpCacheImplementationTest, UpstreamResetWhileExpectingBodyShouldBeInsertFailed) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const std::string body("Hello World");
  auto source = std::make_unique<MockHttpSource>();
  GetBodyCallback get_body_1;
  EXPECT_CALL(*source, getBody(RangeIs(0, Ge(11)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_1 = std::move(cb); });
  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  CacheReaderPtr cache_reader;
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&response_headers), false))
      .WillOnce([&cache_reader](CacheReaderPtr cr, Http::ResponseHeaderMapPtr, bool) {
        cache_reader = std::move(cr);
      });
  cache().insert(dispatcher(), simpleKey(request_path1),
                 Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers), metadata,
                 std::move(source), mock_progress_receiver);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(cache_reader, NotNull());
  ASSERT_THAT(get_body_1, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onInsertFailed);
  get_body_1(nullptr, EndStream::Reset);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
}

TEST_P(HttpCacheImplementationTest, TouchOnExistingEntryHasNoExternallyVisibleEffect) {
  auto key = simpleKey("/name");
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};
  insert(key, response_headers, "");
  cache().touch(key, SystemTime());
}

TEST_P(HttpCacheImplementationTest, TouchOnAbsentEntryHasNoExternallyVisibleEffect) {
  auto key = simpleKey("/name");
  cache().touch(key, SystemTime());
}

TEST_P(HttpCacheImplementationTest, UpstreamResetWhileExpectingTrailersShouldBeInsertFailed) {
  const std::string request_path1("/name");
  LookupResult lookup_result = lookup(request_path1);
  EXPECT_THAT(lookup_result.body_length_, Eq(absl::nullopt));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"}};

  const std::string body("Hello World");
  auto source = std::make_unique<MockHttpSource>();
  GetBodyCallback get_body_1;
  GetTrailersCallback get_trailers;
  EXPECT_CALL(*source, getBody(RangeIs(0, Ge(6)), _))
      .WillOnce([&](AdjustedByteRange, GetBodyCallback cb) { get_body_1 = std::move(cb); });
  EXPECT_CALL(*source, getTrailers(_)).WillOnce([&](GetTrailersCallback cb) {
    get_trailers = std::move(cb);
  });
  const ResponseMetadata metadata{time_system_.systemTime()};
  auto mock_progress_receiver = std::make_shared<MockCacheProgressReceiver>();
  CacheReaderPtr cache_reader;
  EXPECT_CALL(*mock_progress_receiver,
              onHeadersInserted(_, HeaderMapEqualIgnoreOrder(&response_headers), false))
      .WillOnce([&cache_reader](CacheReaderPtr cr, Http::ResponseHeaderMapPtr, bool) {
        cache_reader = std::move(cr);
      });
  cache().insert(dispatcher(), simpleKey(request_path1),
                 Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers), metadata,
                 std::move(source), mock_progress_receiver);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(cache_reader, NotNull());
  ASSERT_THAT(get_body_1, NotNull());
  // Null body + EndStream::More signifies trailers.
  get_body_1(nullptr, EndStream::More);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_progress_receiver.get());
  ASSERT_THAT(get_trailers, NotNull());
  EXPECT_CALL(*mock_progress_receiver, onInsertFailed);
  get_trailers(nullptr, EndStream::Reset);
  pumpDispatcher();
}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
