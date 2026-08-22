#include <functional>

#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache_v2/cache_custom_headers.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"

#include "test/extensions/filters/http/cache_v2/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Between;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::MockFunction;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;

template <typename T> T consumeCallback(T& cb) {
  T ret = std::move(cb);
  cb = nullptr;
  return ret;
}

class CacheSessionsTest : public ::testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
  std::shared_ptr<CacheSessions> cache_sessions_;
  MockHttpCache* mock_http_cache_;
  Http::MockAsyncClient mock_async_client_;
  std::vector<HttpCache::LookupCallback> captured_lookup_callbacks_;
  std::vector<UpstreamRequest*> fake_upstreams_;
  std::vector<Http::RequestHeaderMapPtr> fake_upstream_sent_headers_;
  std::vector<GetHeadersCallback> fake_upstream_get_headers_callbacks_;
  std::shared_ptr<MockCacheableResponseChecker> mock_cacheable_response_checker_ =
      std::make_shared<MockCacheableResponseChecker>();
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context_;

  void advanceTime(std::chrono::milliseconds increment) {
    SystemTime current_time = time_system_.systemTime();
    current_time += increment;
    time_system_.setSystemTime(current_time);
  }

  void SetUp() override {
    EXPECT_CALL(*mock_cacheable_response_checker_, isCacheableResponse)
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    auto mock_http_cache = std::make_unique<MockHttpCache>();
    mock_http_cache_ = mock_http_cache.get();
    cache_sessions_ = CacheSessions::create(mock_factory_context_.server_factory_context_,
                                            std::move(mock_http_cache));
    ON_CALL(*mock_http_cache_, lookup)
        .WillByDefault([this](LookupRequest&&, HttpCache::LookupCallback&& cb) {
          captured_lookup_callbacks_.push_back(std::move(cb));
        });
  }

  void pumpDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  void TearDown() override {
    pumpDispatcher();
    // Any residual cache lookups must complete their callbacks to close
    // out ownership of the CacheSessionsEntries.
    for (auto& cb : captured_lookup_callbacks_) {
      if (cb) {
        // Cache entries will be evicted when cache returns an error for lookup.
        EXPECT_CALL(*mock_http_cache_, evict);
        consumeCallback(cb)(absl::UnknownError("test teardown"));
        pumpDispatcher();
      }
    }
    // Any residual upstreams must complete their callbacks to close out
    // ownership of the CacheSessionsEntries.
    for (auto& cb : fake_upstream_get_headers_callbacks_) {
      if (cb) {
        consumeCallback(cb)(nullptr, EndStream::Reset);
        pumpDispatcher();
      }
    }
  }

  UpstreamRequestFactoryPtr mockUpstreamFactory() {
    auto factory = std::make_unique<MockUpstreamRequestFactory>();
    EXPECT_CALL(*factory, create).WillRepeatedly([this]() -> UpstreamRequestPtr {
      auto upstream_request = std::make_unique<MockUpstreamRequest>();
      fake_upstreams_.emplace_back(upstream_request.get());
      fake_upstream_sent_headers_.push_back(nullptr);
      fake_upstream_get_headers_callbacks_.push_back(nullptr);
      // We can't capture the callback inside the FakeUpstream because that
      // causes an ownership cycle.
      int i = fake_upstreams_.size() - 1;
      EXPECT_CALL(*upstream_request, sendHeaders)
          .WillOnce([this, i](Http::RequestHeaderMapPtr headers) {
            fake_upstream_sent_headers_[i] = std::move(headers);
          });
      EXPECT_CALL(*upstream_request, getHeaders)
          .Times(Between(0, 1))
          .WillRepeatedly([this, i](GetHeadersCallback&& cb) {
            fake_upstream_get_headers_callbacks_[i] = std::move(cb);
          });
      return upstream_request;
    });
    return factory;
  }

  Http::TestRequestHeaderMapImpl requestHeaders(absl::string_view path) {
    return Http::TestRequestHeaderMapImpl{
        {"host", "test_host"}, {":path", std::string{path}}, {":scheme", "https"}};
  }

  ActiveLookupRequestPtr testLookupRequest(Http::RequestHeaderMap& headers) {
    return std::make_unique<ActiveLookupRequest>(
        headers, mockUpstreamFactory(), "test_cluster", *dispatcher_,
        api_->timeSource().systemTime(), mock_cacheable_response_checker_, cache_sessions_, false);
  }

  ActiveLookupRequestPtr testLookupRequest(absl::string_view path) {
    auto headers = requestHeaders(path);
    return testLookupRequest(headers);
  }

  ActiveLookupRequestPtr testLookupRangeRequest(absl::string_view path, int start, int end) {
    auto headers = requestHeaders(path);
    headers.addCopy("range", absl::StrCat("bytes=", start, "-", end));
    return testLookupRequest(headers);
  }

  ActiveLookupRequestPtr testLookupRequestWithNoCache(absl::string_view path) {
    auto headers = requestHeaders(path);
    headers.addCopy("cache-control", "no-cache");
    return testLookupRequest(headers);
  }

  ActiveLookupRequestPtr testLookupRequestWithIfNoneMatch(absl::string_view path,
                                                          absl::string_view etag) {
    auto headers = requestHeaders(path);
    headers.setCopy(Http::CustomHeaders::get().IfNoneMatch, etag);
    return testLookupRequest(headers);
  }
};

Http::ResponseHeaderMapPtr uncacheableResponseHeaders() {
  auto h = std::make_unique<Http::TestResponseHeaderMapImpl>();
  h->addCopy("cache-control", "no-cache");
  return h;
}

static std::string dateNow() {
  static const DateFormatter formatter{"%a, %d %b %Y %H:%M:%S GMT"};
  SystemTime now = Event::SimulatedTimeSystem().systemTime();
  return formatter.fromTime(now);
}

static std::string dateNowPlus60s() {
  static const DateFormatter formatter{"%a, %d %b %Y %H:%M:%S GMT"};
  SystemTime t = Event::SimulatedTimeSystem().systemTime();
  t += std::chrono::seconds(60);
  return formatter.fromTime(t);
}

Http::ResponseHeaderMapPtr cacheableResponseHeaders(std::optional<uint64_t> content_length = 0) {
  auto h = std::make_unique<Http::TestResponseHeaderMapImpl>();
  h->setStatus("200");
  h->addCopy(":scheme", "http");
  h->addCopy(":method", "GET");
  h->addCopy("cache-control", "max-age=86400");
  h->addCopy("date", dateNow());
  if (content_length.has_value()) {
    h->addCopy("content-length", absl::StrCat(content_length.value()));
  }
  return h;
}

Http::ResponseHeaderMapPtr
cacheableResponseHeadersByExpire(std::optional<uint64_t> content_length = 0) {
  auto h = std::make_unique<Http::TestResponseHeaderMapImpl>();
  h->setStatus("200");
  h->addCopy(":scheme", "http");
  h->addCopy(":method", "GET");
  h->addCopy("expires", dateNowPlus60s());
  h->addCopy("date", dateNow());
  if (content_length.has_value()) {
    h->addCopy("content-length", absl::StrCat(content_length.value()));
  }
  return h;
}

inline constexpr auto KeyHasPath = [](const auto& m) { return Property("path", &Key::path, m); };

inline constexpr auto LookupHasKey = [](const auto& m) {
  return Property("key", &LookupRequest::key, m);
};

inline constexpr auto LookupHasPath = [](const auto& m) { return LookupHasKey(KeyHasPath(m)); };

inline constexpr auto RangeIs = [](const auto& m1, const auto& m2) {
  return AllOf(Property("begin", &AdjustedByteRange::begin, m1),
               Property("end", &AdjustedByteRange::end, m2));
};

MATCHER_P(HasNoHeader, key, "") {
  *result_listener << arg;
  return ExplainMatchResult(IsEmpty(), arg.get(::Envoy::Http::LowerCaseString(std::string(key))),
                            result_listener);
}

MATCHER_P(GetResultHasValue, matcher, "") {
  if (!ExplainMatchResult(Property("size", &Http::HeaderMap::GetResult::size, 1), arg,
                          result_listener)) {
    return false;
  }
  return ExplainMatchResult(matcher, arg[0]->value().getStringView(), result_listener);
}

MATCHER_P2(HasHeader, key, matcher, "") {
  *result_listener << arg;
  return ExplainMatchResult(GetResultHasValue(matcher),
                            arg.get(::Envoy::Http::LowerCaseString(std::string(key))),
                            result_listener);
}

TEST_F(CacheSessionsTest, RequestsForSeparateKeysIssueSeparateLookupRequests) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/b"), _));
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/c"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/b"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/c"), _));
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  cache_sessions_->lookup(testLookupRequest("/b"), [](ActiveLookupResultPtr) {});
  cache_sessions_->lookup(testLookupRequest("/c"), [](ActiveLookupResultPtr) {});
  pumpDispatcher();
  EXPECT_THAT(captured_lookup_callbacks_.size(), Eq(3));
}

TEST_F(CacheSessionsTest, MultipleRequestsForSameKeyIssuesOnlyOneLookupRequest) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(3);
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  pumpDispatcher();
  EXPECT_THAT(captured_lookup_callbacks_.size(), Eq(1));
}

TEST_F(CacheSessionsTest, CacheSessionsEntriesExpireOnAdjacentLookup) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _)).Times(2);
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/b"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/b"), _));
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  advanceTime(std::chrono::hours(1));
  // request to adjacent resource to trigger expiry of original.
  cache_sessions_->lookup(testLookupRequest("/b"), [](ActiveLookupResultPtr) {});
  // another request for the original resource should have a new lookup because
  // the old entry should have been removed.
  cache_sessions_->lookup(testLookupRequest("/a"), [](ActiveLookupResultPtr) {});
  pumpDispatcher();
  EXPECT_THAT(captured_lookup_callbacks_.size(), Eq(3));
}

TEST_F(CacheSessionsTest, CacheDeletionDuringLookupStillCompletesLookup) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, evict(_, KeyHasPath("/a")));
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  // cache gets deleted before lookup callback.
  cache_sessions_.reset();
  pumpDispatcher();
  consumeCallback(captured_lookup_callbacks_[0])(absl::UnknownError("cache fail"));
  pumpDispatcher();
  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::LookupError));
  // Should have become an upstream pass-through request.
  EXPECT_THAT(result->http_source_.get(), Eq(fake_upstreams_[0]));
}

TEST_F(CacheSessionsTest, CacheMissWithUncacheableResponseProvokesPassThrough) {
  Mock::VerifyAndClearExpectations(mock_cacheable_response_checker_.get());
  EXPECT_CALL(*mock_cacheable_response_checker_, isCacheableResponse)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(3);
  ActiveLookupResultPtr result1, result2, result3;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  pumpDispatcher();
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(uncacheableResponseHeaders(),
                                                           EndStream::End);
  pumpDispatcher();
  // Uncacheable should have provoked one passthrough upstream request, and
  // given the already existing upstream request to the first result.
  ASSERT_THAT(fake_upstreams_.size(), Eq(2));
  EXPECT_THAT(fake_upstream_sent_headers_[1],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(2));
  // getHeaders should not have been called yet on the second upstream, because
  // that one is handed to the client unused.
  EXPECT_THAT(fake_upstream_get_headers_callbacks_[1], IsNull());
  Http::ResponseHeaderMapPtr headers1, headers2, headers3;
  EXPECT_THAT(result1->status_, Eq(CacheEntryStatus::Uncacheable));
  EXPECT_THAT(result2->status_, Eq(CacheEntryStatus::Uncacheable));
  // First getHeaders should be retrieving the wrapped already-captured headers from
  // the original upstream.
  result1->http_source_->getHeaders(
      [&headers1](Http::ResponseHeaderMapPtr h, EndStream) { headers1 = std::move(h); });
  // Second one should call the upstream, so now we have a captured callback.
  result2->http_source_->getHeaders(
      [&headers2](Http::ResponseHeaderMapPtr h, EndStream) { headers2 = std::move(h); });
  ASSERT_THAT(fake_upstream_get_headers_callbacks_[1], NotNull());
  consumeCallback(fake_upstream_get_headers_callbacks_[1])(uncacheableResponseHeaders(),
                                                           EndStream::End);
  pumpDispatcher();
  EXPECT_THAT(headers1, Pointee(IsSupersetOfHeaders(
                            Http::TestResponseHeaderMapImpl{{"cache-control", "no-cache"}})));
  EXPECT_THAT(headers2, Pointee(IsSupersetOfHeaders(
                            Http::TestResponseHeaderMapImpl{{"cache-control", "no-cache"}})));
  // Finally, a subsequent request should also be pass-through with no lookup required.
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result3](ActiveLookupResultPtr r) { result3 = std::move(r); });
  pumpDispatcher();
  ASSERT_THAT(result3, NotNull());
  EXPECT_THAT(result3->status_, Eq(CacheEntryStatus::Uncacheable));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(3));
  result3->http_source_->getHeaders(
      [&headers3](Http::ResponseHeaderMapPtr h, EndStream) { headers3 = std::move(h); });
  consumeCallback(fake_upstream_get_headers_callbacks_[2])(uncacheableResponseHeaders(),
                                                           EndStream::End);
  pumpDispatcher();
  EXPECT_THAT(headers3, Pointee(IsSupersetOfHeaders(
                            Http::TestResponseHeaderMapImpl{{"cache-control", "no-cache"}})));
}

TEST_F(CacheSessionsTest, CacheMissWithCacheableResponseProvokesSharedInsertStream) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(3);
  ActiveLookupResultPtr result1, result2, result3;
  auto response_headers = cacheableResponseHeaders();
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  std::shared_ptr<CacheProgressReceiver> progress;
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  ASSERT_THAT(progress, NotNull());
  progress->onHeadersInserted(std::make_unique<MockCacheReader>(),
                              Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers),
                              true);
  pumpDispatcher();
  ASSERT_THAT(result1, NotNull());
  // First result should be cache miss because it triggered insertion.
  EXPECT_THAT(result1->status_, Eq(CacheEntryStatus::Miss));
  ASSERT_THAT(result2, NotNull());
  // Second result should be a follower from the insertion.
  EXPECT_THAT(result2->status_, Eq(CacheEntryStatus::Follower));
  // Request after insert is complete should be able to lookup immediately.
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result3](ActiveLookupResultPtr r) { result3 = std::move(r); });
  pumpDispatcher();
  ASSERT_THAT(result3, NotNull());
  EXPECT_THAT(result3->status_, Eq(CacheEntryStatus::Hit));
  // And get headers immediately too.
  Http::ResponseHeaderMapPtr headers3;
  EndStream end_stream;
  result3->http_source_->getHeaders([&](Http::ResponseHeaderMapPtr headers, EndStream es) {
    headers3 = std::move(headers);
    end_stream = es;
  });
  EXPECT_THAT(headers3, Pointee(IsSupersetOfHeaders(*response_headers)));
  EXPECT_THAT(end_stream, Eq(EndStream::End));
}

TEST_F(CacheSessionsTest,
       CacheMissWithCacheableResponseProvokesSharedInsertStreamWithBodyAndTrailers) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(3);
  ActiveLookupResultPtr result1, result2, result3;
  auto response_headers = cacheableResponseHeaders();
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  std::shared_ptr<CacheProgressReceiver> progress;
  MockCacheReader* mock_cache_reader;
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, NotNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::More);
  pumpDispatcher();
  // The upstream was given to the cache; since it's a fake we can forget about
  // that and just have the cache complete its write operations when we choose.
  ASSERT_THAT(progress, NotNull());
  {
    auto m = std::make_unique<MockCacheReader>();
    mock_cache_reader = m.get();
    progress->onHeadersInserted(
        std::move(m), Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), false);
  }
  pumpDispatcher();
  ASSERT_THAT(result1, NotNull());
  // First result should be cache miss because it triggered insertion.
  EXPECT_THAT(result1->status_, Eq(CacheEntryStatus::Miss));
  ASSERT_THAT(result2, NotNull());
  // Second result should be a follower from the existing insertion.
  EXPECT_THAT(result2->status_, Eq(CacheEntryStatus::Follower));
  // Request after header-insert is complete should be able to lookup immediately.
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result3](ActiveLookupResultPtr r) { result3 = std::move(r); });
  pumpDispatcher();
  ASSERT_THAT(result3, NotNull());
  EXPECT_THAT(result3->status_, Eq(CacheEntryStatus::Hit));
  // And get headers immediately too.
  Http::ResponseHeaderMapPtr headers3;
  EndStream end_stream;
  result3->http_source_->getHeaders([&](Http::ResponseHeaderMapPtr headers, EndStream es) {
    headers3 = std::move(headers);
    end_stream = es;
  });
  pumpDispatcher();
  EXPECT_THAT(headers3, Pointee(IsSupersetOfHeaders(*response_headers)));
  EXPECT_THAT(end_stream, Eq(EndStream::More));
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_callback1, body_callback2, body_callback3;
  result1->http_source_->getBody(AdjustedByteRange(0, 5), body_callback1.AsStdFunction());
  result2->http_source_->getBody(AdjustedByteRange(0, 2), body_callback2.AsStdFunction());
  result3->http_source_->getBody(AdjustedByteRange(1, 5), body_callback3.AsStdFunction());
  EXPECT_CALL(*mock_cache_reader, getBody(_, RangeIs(0, 3), _))
      .WillOnce([&](Event::Dispatcher&, AdjustedByteRange, GetBodyCallback&& cb) {
        cb(std::make_unique<Buffer::OwnedImpl>("abc"), EndStream::More);
      });
  EXPECT_CALL(body_callback1, Call(Pointee(BufferString("abc")), EndStream::More));
  EXPECT_CALL(body_callback2, Call(Pointee(BufferString("ab")), EndStream::More));
  EXPECT_CALL(body_callback3, Call(Pointee(BufferString("bc")), EndStream::More));
  progress->onBodyInserted(AdjustedByteRange(0, 3), false);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_cache_reader);
  Mock::VerifyAndClearExpectations(&body_callback1);
  Mock::VerifyAndClearExpectations(&body_callback2);
  Mock::VerifyAndClearExpectations(&body_callback3);
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_callback4, body_callback5, body_callback6;
  result1->http_source_->getBody(AdjustedByteRange(3, 5), body_callback4.AsStdFunction());
  result2->http_source_->getBody(AdjustedByteRange(3, 5), body_callback5.AsStdFunction());
  // Issuing a request for body that's in the cache, while other requests are still awaiting
  // body that is not yet in the cache, should skip the queue.
  EXPECT_CALL(*mock_cache_reader, getBody(_, RangeIs(0, 3), _))
      .WillOnce([&](Event::Dispatcher&, AdjustedByteRange, GetBodyCallback&& cb) {
        cb(std::make_unique<Buffer::OwnedImpl>("abc"), EndStream::More);
      });
  EXPECT_CALL(body_callback6, Call(Pointee(BufferString("abc")), EndStream::More));
  result3->http_source_->getBody(AdjustedByteRange(0, 3), body_callback6.AsStdFunction());
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(&body_callback6);
  Mock::VerifyAndClearExpectations(mock_cache_reader);
  // Finally, insert completing should post to the queued requests.
  EXPECT_CALL(*mock_cache_reader, getBody(_, RangeIs(3, 5), _))
      .WillOnce([&](Event::Dispatcher&, AdjustedByteRange, GetBodyCallback&& cb) {
        cb(std::make_unique<Buffer::OwnedImpl>("de"), EndStream::More);
      });
  EXPECT_CALL(body_callback4, Call(Pointee(BufferString("de")), EndStream::More));
  EXPECT_CALL(body_callback5, Call(Pointee(BufferString("de")), EndStream::More));
  progress->onBodyInserted(AdjustedByteRange(3, 5), false);
  pumpDispatcher();
  Http::TestResponseTrailerMapImpl trailers{{"x-test", "yes"}};
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailers_callback1, trailers_callback2;
  result1->http_source_->getTrailers(trailers_callback1.AsStdFunction());
  pumpDispatcher();
  EXPECT_CALL(trailers_callback1, Call(Pointee(IsSupersetOfHeaders(trailers)), EndStream::End));
  progress->onTrailersInserted(Http::createHeaderMap<Http::ResponseTrailerMapImpl>(trailers));
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(&trailers_callback1);
  EXPECT_CALL(trailers_callback2, Call(Pointee(IsSupersetOfHeaders(trailers)), EndStream::End));
  result2->http_source_->getTrailers(trailers_callback2.AsStdFunction());
  pumpDispatcher();
}

TEST_F(CacheSessionsTest, CacheHitGoesDirectlyToCachedResponses) {
  auto response_headers = cacheableResponseHeaders();
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();
  MockCacheReader* mock_cache_reader;
  // Cache hit.
  Http::TestResponseTrailerMapImpl response_trailers{{"x-test", "yes"}};
  {
    auto m = std::make_unique<MockCacheReader>();
    mock_cache_reader = m.get();
    ResponseMetadata metadata;
    metadata.response_time_ = api_->timeSource().systemTime();
    consumeCallback(captured_lookup_callbacks_[0])(LookupResult{
        std::move(m),
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers),
        Http::createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers),
        std::move(metadata),
        5,
    });
  }
  pumpDispatcher();
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::Hit));
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_callback;
  EXPECT_CALL(header_callback,
              Call(Pointee(IsSupersetOfHeaders(*response_headers)), EndStream::More));
  result->http_source_->getHeaders(header_callback.AsStdFunction());
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_callback1, body_callback2;
  EXPECT_CALL(body_callback1, Call(Pointee(BufferString("abcde")), EndStream::More));
  EXPECT_CALL(*mock_cache_reader, getBody(_, RangeIs(0, 5), _))
      .WillOnce([&](Event::Dispatcher&, AdjustedByteRange, GetBodyCallback cb) {
        cb(std::make_unique<Buffer::OwnedImpl>("abcde"), EndStream::More);
      });
  result->http_source_->getBody(AdjustedByteRange(0, 9999), body_callback1.AsStdFunction());
  pumpDispatcher();
  // Asking for more body when there is no more returns a nullptr indicating it's
  // time for trailers.
  EXPECT_CALL(body_callback2, Call(IsNull(), EndStream::More));
  result->http_source_->getBody(AdjustedByteRange(5, 9999), body_callback2.AsStdFunction());
  pumpDispatcher();
  // Then finally the 'filter' asks for trailers, and gets them back immediately.
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_callback;
  EXPECT_CALL(trailer_callback,
              Call(Pointee(IsSupersetOfHeaders(response_trailers)), EndStream::End));
  result->http_source_->getTrailers(trailer_callback.AsStdFunction());
  pumpDispatcher();
}

TEST_F(CacheSessionsTest, CacheInsertFailurePassesThroughLookupsAndWillLookupAgain) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);
  ActiveLookupResultPtr result1, result2, result3;
  auto response_headers = cacheableResponseHeadersByExpire();
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  std::shared_ptr<CacheProgressReceiver> progress;
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_http_cache_);
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, evict(_, KeyHasPath("/a")));
  progress->onInsertFailed(absl::InternalError("test error"));
  pumpDispatcher();
  ASSERT_THAT(result1->http_source_, NotNull());
  ASSERT_THAT(result2->http_source_, NotNull());
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_callback1, header_callback2;
  EXPECT_CALL(header_callback1,
              Call(Pointee(Http::IsSupersetOfHeaders(*response_headers)), EndStream::End));
  EXPECT_CALL(header_callback2,
              Call(Pointee(Http::IsSupersetOfHeaders(*response_headers)), EndStream::End));
  result1->http_source_->getHeaders(header_callback1.AsStdFunction());
  result2->http_source_->getHeaders(header_callback2.AsStdFunction());
  // Both requests should have a fresh upstream for pass-through.
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(3));
  consumeCallback(fake_upstream_get_headers_callbacks_[1])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  consumeCallback(fake_upstream_get_headers_callbacks_[2])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  // A new request should provoke a new lookup because the previous insertion failed.
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result3](ActiveLookupResultPtr r) { result3 = std::move(r); });
  pumpDispatcher();
  // Should have sent a second lookup.
  ASSERT_THAT(captured_lookup_callbacks_.size(), Eq(2));
  // Cache miss again.
  consumeCallback(captured_lookup_callbacks_[1])(LookupResult{});
  pumpDispatcher();
  // Should be the original request, the two that pass-through, and the new request.
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(4));
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[3])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_http_cache_);
  EXPECT_CALL(*mock_http_cache_, evict(_, KeyHasPath("/a")));
  progress->onInsertFailed(absl::InternalError("test error"));
  pumpDispatcher();
  ASSERT_THAT(result3->http_source_, NotNull());
  // Should be yet another upstream request for the new pass-through.
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(5));
}

TEST_F(CacheSessionsTest, CacheInsertFailureResetsStreamingContexts) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);
  ActiveLookupResultPtr result1, result2;
  auto response_headers = cacheableResponseHeaders();
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}})));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  std::shared_ptr<CacheProgressReceiver> progress;
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_http_cache_);
  EXPECT_CALL(*mock_http_cache_, evict(_, KeyHasPath("/a")));
  progress->onHeadersInserted(std::make_unique<MockCacheReader>(),
                              Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers),
                              false);
  pumpDispatcher();
  ASSERT_THAT(result1->http_source_, NotNull());
  ASSERT_THAT(result2->http_source_, NotNull());
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_callback;
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailers_callback;
  result1->http_source_->getBody(AdjustedByteRange(0, 5), body_callback.AsStdFunction());
  result2->http_source_->getTrailers(trailers_callback.AsStdFunction());
  EXPECT_CALL(body_callback, Call(IsNull(), EndStream::Reset));
  EXPECT_CALL(trailers_callback, Call(IsNull(), EndStream::Reset));
  progress->onInsertFailed(absl::InternalError("test error"));
  pumpDispatcher();
}

TEST_F(CacheSessionsTest, MismatchedSizeAndContentLengthFromUpstreamLogsAnError) {
  EXPECT_LOG_CONTAINS(
      "error", "cache insert for test_host/a had content-length header 5 but actual size 3", {
        EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
        EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
        ActiveLookupResultPtr result1;
        auto response_headers = cacheableResponseHeaders(5);
        cache_sessions_->lookup(testLookupRequest("/a"),
                                [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
        pumpDispatcher();
        // Cache miss.
        consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
        pumpDispatcher();
        // Upstream request should have been sent.
        ASSERT_THAT(fake_upstreams_.size(), Eq(1));
        std::shared_ptr<CacheProgressReceiver> progress;
        // Cacheable response.
        EXPECT_CALL(*mock_http_cache_,
                    insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _,
                           NotNull(), _))
            .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                          HttpSourcePtr, std::shared_ptr<CacheProgressReceiver> receiver) {
              progress = receiver;
            });
        consumeCallback(fake_upstream_get_headers_callbacks_[0])(
            Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::More);
        pumpDispatcher();
        // The upstream was given to the cache; since it's a fake we can forget about
        // that and just have the cache complete its write operations when we choose.
        ASSERT_THAT(progress, NotNull());
        progress->onHeadersInserted(
            std::make_unique<MockCacheReader>(),
            Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), false);
        pumpDispatcher();
        // Actual body only 3 bytes despite content-length 5.
        progress->onBodyInserted(AdjustedByteRange(0, 3), true);
        pumpDispatcher();
      });
}

TEST_F(CacheSessionsTest, RangeRequestMissGetsFullResourceFromUpstreamAndServesRanges) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);
  ActiveLookupResultPtr result1, result2;
  auto response_headers = cacheableResponseHeaders(1024);
  cache_sessions_->lookup(testLookupRangeRequest("/a", 0, 5),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  cache_sessions_->lookup(testLookupRangeRequest("/a", 5, 10),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  // Upstream request should have had the range header removed.
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(AllOf(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}}),
                            HasNoHeader("range"))));
  std::shared_ptr<CacheProgressReceiver> progress;
  // Cacheable response.
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_http_cache_);
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> headers_callback1, headers_callback2;
  progress->onHeadersInserted(std::unique_ptr<MockCacheReader>(),
                              Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers),
                              false);
  pumpDispatcher();
  EXPECT_CALL(headers_callback1,
              Call(Pointee(AllOf(HasHeader(":status", "206"), HasHeader("content-length", "6"),
                                 HasHeader("content-range", "bytes 0-5/1024"))),
                   EndStream::More));
  EXPECT_CALL(headers_callback2,
              Call(Pointee(AllOf(HasHeader(":status", "206"), HasHeader("content-length", "6"),
                                 HasHeader("content-range", "bytes 5-10/1024"))),
                   EndStream::More));
  ASSERT_THAT(result1, NotNull());
  result1->http_source_->getHeaders(headers_callback1.AsStdFunction());
  result2->http_source_->getHeaders(headers_callback2.AsStdFunction());
  Mock::VerifyAndClearExpectations(&headers_callback1);
  Mock::VerifyAndClearExpectations(&headers_callback2);
  // No need to test the body behavior here because it's no different than
  // how body ranges are requested by any other request - the difference
  // in behavior there is controlled by the filter which is outside the scope
  // of CacheSessions unit tests.
}

TEST_F(CacheSessionsTest, RangeRequestWhenLengthIsUnknownReturnsNotSatisfiable) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  ActiveLookupResultPtr result1;
  auto response_headers = cacheableResponseHeaders(0);
  cache_sessions_->lookup(testLookupRangeRequest("/a", 0, 5),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  pumpDispatcher();
  // Cache miss.
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  // Upstream request should have had the range header removed.
  EXPECT_THAT(fake_upstream_sent_headers_[0],
              Pointee(AllOf(IsSupersetOfHeaders(Http::TestRequestHeaderMapImpl{{":path", "/a"}}),
                            HasNoHeader("range"))));
  std::shared_ptr<CacheProgressReceiver> progress;
  // Cacheable response.
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                    HttpSourcePtr,
                    std::shared_ptr<CacheProgressReceiver> receiver) { progress = receiver; });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();
  Mock::VerifyAndClearExpectations(mock_http_cache_);
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> headers_callback1;
  progress->onHeadersInserted(std::unique_ptr<MockCacheReader>(),
                              Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers),
                              false);
  pumpDispatcher();
  EXPECT_CALL(headers_callback1, Call(Pointee(HasHeader(":status", "416")), EndStream::End));
  ASSERT_THAT(result1, NotNull());
  result1->http_source_->getHeaders(headers_callback1.AsStdFunction());
  Mock::VerifyAndClearExpectations(&headers_callback1);
}

TEST_F(CacheSessionsTest, PassthroughWithUpstreamResetCallsGetHeadersCallbackWithNullPointer) {
  Mock::VerifyAndClearExpectations(mock_cacheable_response_checker_.get());
  EXPECT_CALL(*mock_cacheable_response_checker_, isCacheableResponse)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(false));
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);

  ActiveLookupResultPtr result1;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result1](ActiveLookupResultPtr r) { result1 = std::move(r); });
  pumpDispatcher();
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  // Upstream request should have been sent.
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(uncacheableResponseHeaders(),
                                                           EndStream::End);
  pumpDispatcher();
  ASSERT_THAT(result1, NotNull());
  EXPECT_THAT(result1->status_, Eq(CacheEntryStatus::Uncacheable));

  ActiveLookupResultPtr result2;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result2](ActiveLookupResultPtr r) { result2 = std::move(r); });
  pumpDispatcher();
  ASSERT_THAT(result2, NotNull());
  EXPECT_THAT(result2->status_, Eq(CacheEntryStatus::Uncacheable));
  ASSERT_THAT(fake_upstreams_.size(), Eq(2));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(2));
  EXPECT_THAT(fake_upstream_get_headers_callbacks_[1], IsNull());
  Http::ResponseHeaderMapPtr headers2;
  result2->http_source_->getHeaders(
      [&headers2](Http::ResponseHeaderMapPtr h, EndStream) { headers2 = std::move(h); });
  ASSERT_THAT(fake_upstream_get_headers_callbacks_[1], NotNull());
  // Simulate an upstream reset by calling the get headers callback with a nullptr and reset end
  // stream.
  consumeCallback(fake_upstream_get_headers_callbacks_[1])(nullptr, EndStream::Reset);
  EXPECT_THAT(headers2, IsNull());
}

TEST_F(CacheSessionsTest, UpstreamResetDuringCacheMissReportsUpstreamReset) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(nullptr, EndStream::Reset);
  pumpDispatcher();
  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::UpstreamReset));
}

TEST_F(CacheSessionsTest, VaryHeaderInUpstreamResponseTreatedAsUncacheable) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest("/a"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  ASSERT_THAT(fake_upstreams_.size(), Eq(1));
  ASSERT_THAT(fake_upstream_get_headers_callbacks_.size(), Eq(1));
  auto headers = cacheableResponseHeaders();
  headers->addCopy(Http::CustomHeaders::get().Vary, "accept");
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(headers), EndStream::End);
  pumpDispatcher();
  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::Uncacheable));
}

// Downstream If-None-Match handling. These tests cover cache fill request sanitization, fresh
// cache hits, combined field lines, wildcard matching, explicit and stale-entry revalidation,
// validator fallback, replacement representations returned by upstream, and request collapsing
// across lookup, insertion, validation, and pass-through states.

TEST_F(CacheSessionsTest, CacheMissRemovesIfNoneMatchFromCacheFillRequest) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("client-value")"),
                          [](ActiveLookupResultPtr) {});
  pumpDispatcher();
  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasNoHeader("if-none-match"));
}

TEST_F(CacheSessionsTest, IfNoneMatchIsEvaluatedForEachCollapsedRequest) {
  auto response_headers = cacheableResponseHeaders(5);
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(3);

  ActiveLookupResultPtr weak_match, mismatch, wildcard;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"(W/"selected")"),
                          [&weak_match](ActiveLookupResultPtr r) { weak_match = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("different")"),
                          [&mismatch](ActiveLookupResultPtr r) { mismatch = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", "*"),
                          [&wildcard](ActiveLookupResultPtr r) { wildcard = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime();
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 5});
  pumpDispatcher();

  ASSERT_THAT(weak_match, NotNull());
  EXPECT_THAT(weak_match->status_, Eq(CacheEntryStatus::FoundNotModified));
  ASSERT_THAT(mismatch, NotNull());
  EXPECT_THAT(mismatch->status_, Eq(CacheEntryStatus::Hit));
  ASSERT_THAT(wildcard, NotNull());
  EXPECT_THAT(wildcard->status_, Eq(CacheEntryStatus::FoundNotModified));

  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> weak_match_headers;
  EXPECT_CALL(weak_match_headers,
              Call(Pointee(AllOf(HasHeader(":status", "304"), HasNoHeader("content-length"))),
                   EndStream::End));
  weak_match->http_source_->getHeaders(weak_match_headers.AsStdFunction());

  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> mismatch_headers;
  EXPECT_CALL(mismatch_headers,
              Call(Pointee(AllOf(HasHeader(":status", "200"), HasHeader("content-length", "5"))),
                   EndStream::More));
  mismatch->http_source_->getHeaders(mismatch_headers.AsStdFunction());
}

TEST_F(CacheSessionsTest, IfNoneMatchCombinesMultipleFieldLines) {
  auto response_headers = cacheableResponseHeaders();
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  auto request_headers = requestHeaders("/a");
  request_headers.addCopy(Http::CustomHeaders::get().IfNoneMatch, R"("different")");
  request_headers.addCopy(Http::CustomHeaders::get().IfNoneMatch, R"(W/"selected")");
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest(request_headers),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime();
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 0});
  pumpDispatcher();

  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::FoundNotModified));
}

TEST_F(CacheSessionsTest, IfNoneMatchWildcardMatchesCachedResponseWithoutEtag) {
  auto response_headers = cacheableResponseHeaders();
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", "*"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime();
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 0});
  pumpDispatcher();

  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::FoundNotModified));
}

TEST_F(CacheSessionsTest, StaleCollapsedRequestsUseCachedValidatorThenEvaluateIndividually) {
  auto response_headers = cacheableResponseHeaders(5);
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  response_headers->setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=0");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);

  ActiveLookupResultPtr mismatch, match;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("client-value")"),
                          [&mismatch](ActiveLookupResultPtr r) { mismatch = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("selected")"),
                          [&match](ActiveLookupResultPtr r) { match = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime() - std::chrono::seconds(1);
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 5});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  // Cache validation must use the cached representation's ETag rather than the first downstream
  // request's ETag.
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasHeader("if-none-match", R"("selected")"));

  auto validation_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  validation_headers->setStatus(304);
  EXPECT_CALL(*mock_http_cache_, updateHeaders(_, KeyHasPath("/a"), _, _));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(validation_headers),
                                                           EndStream::End);
  pumpDispatcher();

  // Each downstream condition is evaluated after the shared validation completes.
  ASSERT_THAT(mismatch, NotNull());
  EXPECT_THAT(mismatch->status_, Eq(CacheEntryStatus::Validated));
  ASSERT_THAT(match, NotNull());
  EXPECT_THAT(match->status_, Eq(CacheEntryStatus::FoundNotModified));
}

TEST_F(CacheSessionsTest, IfNoneMatchWithNoCacheRevalidatesFreshEntryBeforeResponding) {
  auto response_headers = cacheableResponseHeaders();
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  auto request_headers = requestHeaders("/a");
  request_headers.setCopy(Http::CustomHeaders::get().IfNoneMatch, R"("selected")");
  request_headers.setCopy(Http::CustomHeaders::get().CacheControl, "no-cache");
  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequest(request_headers),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime();
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 0});
  pumpDispatcher();

  ASSERT_THAT(result, IsNull());
  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasHeader("if-none-match", R"("selected")"));

  auto validation_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  validation_headers->setStatus(304);
  EXPECT_CALL(*mock_http_cache_, updateHeaders(_, KeyHasPath("/a"), _, _));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(validation_headers),
                                                           EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::FoundNotModified));
}

TEST_F(CacheSessionsTest, StaleIfNoneMatchWithoutCachedEtagUsesLastModifiedForValidation) {
  constexpr absl::string_view last_modified = "Sun, 06 Nov 1994 08:49:37 GMT";
  auto response_headers = cacheableResponseHeaders(5);
  response_headers->setInline(CacheCustomHeaders::lastModified(), last_modified);
  response_headers->setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=0");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("client-value")"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime() - std::chrono::seconds(1);
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 5});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasNoHeader("if-none-match"));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasHeader("if-modified-since", last_modified));

  auto validation_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  validation_headers->setStatus(304);
  validation_headers->setInline(CacheCustomHeaders::lastModified(), last_modified);
  EXPECT_CALL(*mock_http_cache_, updateHeaders(_, KeyHasPath("/a"), _, _));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(validation_headers),
                                                           EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::Validated));
}

TEST_F(CacheSessionsTest, StaleIfNoneMatchWithoutEtagOrLastModifiedUsesDateForValidation) {
  constexpr absl::string_view date = "Sun, 06 Nov 1994 08:49:37 GMT";
  auto response_headers = cacheableResponseHeaders(5);
  response_headers->setDate(date);
  response_headers->setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=0");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _));

  ActiveLookupResultPtr result;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("client-value")"),
                          [&result](ActiveLookupResultPtr r) { result = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime() - std::chrono::seconds(1);
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 5});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasNoHeader("if-none-match"));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasHeader("if-modified-since", date));

  auto validation_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  validation_headers->setStatus(304);
  EXPECT_CALL(*mock_http_cache_, updateHeaders(_, KeyHasPath("/a"), _, _));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(validation_headers),
                                                           EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->status_, Eq(CacheEntryStatus::Validated));
}

TEST_F(CacheSessionsTest, StaleIfNoneMatchEvaluatesNewRepresentationAfterValidationReturnsOk) {
  auto cached_response_headers = cacheableResponseHeaders();
  cached_response_headers->setInline(CacheCustomHeaders::etag(), R"("old")");
  cached_response_headers->setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=0");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);
  EXPECT_CALL(*mock_http_cache_, evict(_, KeyHasPath("/a")));

  ActiveLookupResultPtr match, mismatch;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("new")"),
                          [&match](ActiveLookupResultPtr r) { match = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("different")"),
                          [&mismatch](ActiveLookupResultPtr r) { mismatch = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime() - std::chrono::seconds(1);
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*cached_response_headers),
                   nullptr, std::move(metadata), 0});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasHeader("if-none-match", R"("old")"));

  auto new_response_headers = cacheableResponseHeaders();
  new_response_headers->setInline(CacheCustomHeaders::etag(), R"("new")");
  std::shared_ptr<CacheProgressReceiver> progress;
  EXPECT_CALL(*mock_http_cache_,
              insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*new_response_headers)), _,
                     IsNull(), _))
      .WillOnce([&progress](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                            HttpSourcePtr, std::shared_ptr<CacheProgressReceiver> receiver) {
        progress = std::move(receiver);
      });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*new_response_headers), EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(progress, NotNull());
  progress->onHeadersInserted(
      nullptr, Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*new_response_headers), true);
  pumpDispatcher();

  ASSERT_THAT(match, NotNull());
  EXPECT_THAT(match->status_, Eq(CacheEntryStatus::FoundNotModified));
  ASSERT_THAT(mismatch, NotNull());
  EXPECT_THAT(mismatch->status_, Eq(CacheEntryStatus::Follower));
}

TEST_F(CacheSessionsTest, IfNoneMatchCollapsedCacheMissEvaluatesInsertedRepresentation) {
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);

  ActiveLookupResultPtr match, mismatch;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("selected")"),
                          [&match](ActiveLookupResultPtr r) { match = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("different")"),
                          [&mismatch](ActiveLookupResultPtr r) { mismatch = std::move(r); });
  pumpDispatcher();

  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasNoHeader("if-none-match"));

  auto response_headers = cacheableResponseHeaders();
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  std::shared_ptr<CacheProgressReceiver> progress;
  EXPECT_CALL(
      *mock_http_cache_,
      insert(_, KeyHasPath("/a"), Pointee(IsSupersetOfHeaders(*response_headers)), _, IsNull(), _))
      .WillOnce([&progress](Event::Dispatcher&, Key, Http::ResponseHeaderMapPtr, ResponseMetadata,
                            HttpSourcePtr, std::shared_ptr<CacheProgressReceiver> receiver) {
        progress = std::move(receiver);
      });
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(progress, NotNull());
  progress->onHeadersInserted(
      nullptr, Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), true);
  pumpDispatcher();

  ASSERT_THAT(match, NotNull());
  EXPECT_THAT(match->status_, Eq(CacheEntryStatus::FoundNotModified));
  ASSERT_THAT(mismatch, NotNull());
  EXPECT_THAT(mismatch->status_, Eq(CacheEntryStatus::Follower));
}

TEST_F(CacheSessionsTest, IfNoneMatchSubscriberJoinsValidationAlreadyInFlight) {
  auto response_headers = cacheableResponseHeaders();
  response_headers->setInline(CacheCustomHeaders::etag(), R"("selected")");
  response_headers->setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=0");
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);

  ActiveLookupResultPtr match, mismatch;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("selected")"),
                          [&match](ActiveLookupResultPtr r) { match = std::move(r); });
  pumpDispatcher();

  ResponseMetadata metadata;
  metadata.response_time_ = api_->timeSource().systemTime() - std::chrono::seconds(1);
  consumeCallback(captured_lookup_callbacks_[0])(
      LookupResult{std::make_unique<MockCacheReader>(),
                   Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers), nullptr,
                   std::move(metadata), 0});
  pumpDispatcher();

  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("different")"),
                          [&mismatch](ActiveLookupResultPtr r) { mismatch = std::move(r); });
  pumpDispatcher();
  EXPECT_THAT(fake_upstream_sent_headers_, testing::SizeIs(1));

  auto validation_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  validation_headers->setStatus(304);
  EXPECT_CALL(*mock_http_cache_, updateHeaders(_, KeyHasPath("/a"), _, _));
  consumeCallback(fake_upstream_get_headers_callbacks_[0])(std::move(validation_headers),
                                                           EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(match, NotNull());
  EXPECT_THAT(match->status_, Eq(CacheEntryStatus::FoundNotModified));
  ASSERT_THAT(mismatch, NotNull());
  EXPECT_THAT(mismatch->status_, Eq(CacheEntryStatus::ValidatedFree));
}

TEST_F(CacheSessionsTest,
       IfNoneMatchUncacheableFillRetriesEveryCollapsedRequestWithOriginalHeader) {
  Mock::VerifyAndClearExpectations(mock_cacheable_response_checker_.get());
  EXPECT_CALL(*mock_cacheable_response_checker_, isCacheableResponse)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*mock_http_cache_, lookup(LookupHasPath("/a"), _));
  EXPECT_CALL(*mock_http_cache_, touch(KeyHasPath("/a"), _)).Times(2);

  ActiveLookupResultPtr first, second;
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("first")"),
                          [&first](ActiveLookupResultPtr r) { first = std::move(r); });
  cache_sessions_->lookup(testLookupRequestWithIfNoneMatch("/a", R"("second")"),
                          [&second](ActiveLookupResultPtr r) { second = std::move(r); });
  pumpDispatcher();

  consumeCallback(captured_lookup_callbacks_[0])(LookupResult{});
  pumpDispatcher();
  ASSERT_THAT(fake_upstream_sent_headers_, ElementsAre(NotNull()));
  EXPECT_THAT(*fake_upstream_sent_headers_[0], HasNoHeader("if-none-match"));

  consumeCallback(fake_upstream_get_headers_callbacks_[0])(uncacheableResponseHeaders(),
                                                           EndStream::End);
  pumpDispatcher();

  ASSERT_THAT(first, NotNull());
  ASSERT_THAT(second, NotNull());
  EXPECT_THAT(first->status_, Eq(CacheEntryStatus::Uncacheable));
  EXPECT_THAT(second->status_, Eq(CacheEntryStatus::Uncacheable));
  ASSERT_THAT(fake_upstream_sent_headers_, testing::SizeIs(3));
  EXPECT_THAT(*fake_upstream_sent_headers_[1], HasHeader("if-none-match", R"("first")"));
  EXPECT_THAT(*fake_upstream_sent_headers_[2], HasHeader("if-none-match", R"("second")"));
}

// TODO: UpdateHeadersSkipSpecificHeaders
// TODO: Vary

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
