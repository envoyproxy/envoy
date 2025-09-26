#include <functional>

#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache_v2/cache_filter.h"

#include "test/extensions/filters/http/cache_v2/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::_;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Property;
using ::testing::Return;

class CacheFilterTest : public ::testing::Test {
protected:
  CacheFilterSharedPtr makeFilter(std::shared_ptr<CacheSessions> cache, bool auto_destroy = true) {
    auto config = std::make_shared<CacheFilterConfig>(config_, std::move(cache),
                                                      context_.server_factory_context_);
    std::shared_ptr<CacheFilter> filter(new CacheFilter(config), [auto_destroy](CacheFilter* f) {
      if (auto_destroy) {
        f->onDestroy();
      }
      delete f;
    });
    filter->setDecoderFilterCallbacks(decoder_callbacks_);
    filter->setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  void SetUp() override {
    ON_CALL(encoder_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    ON_CALL(decoder_callbacks_.stream_info_, filterState())
        .WillByDefault(::testing::ReturnRef(filter_state_));
    // Initialize the time source (otherwise it returns the real time)
    time_source_.setSystemTime(std::chrono::hours(1));
    // Use the initialized time source to set the response date header
    response_headers_.setDate(formatter_.now(time_source_));
    ON_CALL(*mock_cache_, lookup)
        .WillByDefault([this](ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) {
          captured_lookup_request_ = std::move(request);
          captured_lookup_callback_ = std::move(cb);
        });
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    ON_CALL(*mock_http_source_, getHeaders).WillByDefault([this](GetHeadersCallback&& cb) {
      EXPECT_THAT(captured_get_headers_callback_, IsNull());
      captured_get_headers_callback_ = std::move(cb);
    });
    ON_CALL(*mock_http_source_, getBody)
        .WillByDefault([this](AdjustedByteRange, GetBodyCallback&& cb) {
          // getBody can be called multiple times so overwriting body callback makes sense.
          captured_get_body_callback_ = std::move(cb);
        });
    ON_CALL(*mock_http_source_, getTrailers).WillByDefault([this](GetTrailersCallback&& cb) {
      EXPECT_THAT(captured_get_trailers_callback_, IsNull());
      captured_get_trailers_callback_ = std::move(cb);
    });
  }

  void pumpDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  envoy::extensions::filters::http::cache_v2::v3::CacheV2Config config_;
  std::shared_ptr<StreamInfo::FilterState> filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {"host", "fake_host"}, {":method", "GET"}, {":scheme", "https"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"cache-control", "public,max-age=3600"}};
  Http::TestResponseTrailerMapImpl response_trailers_{{"x-test-trailer", "yes"}};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
  std::shared_ptr<MockCacheSessions> mock_cache_ = std::make_shared<MockCacheSessions>();
  std::unique_ptr<MockHttpSource> mock_http_source_ = std::make_unique<MockHttpSource>();
  MockCacheFilterStats& stats() { return mock_cache_->mock_stats_; }
  ActiveLookupRequestPtr captured_lookup_request_;
  ActiveLookupResultCallback captured_lookup_callback_;
  GetHeadersCallback captured_get_headers_callback_;
  GetBodyCallback captured_get_body_callback_;
  GetTrailersCallback captured_get_trailers_callback_;
};
class CacheFilterDeathTest : public CacheFilterTest {};

MATCHER_P(RangeStartsWith, v, "") {
  return ::testing::ExplainMatchResult(::testing::Property("begin", &AdjustedByteRange::begin, v),
                                       arg, result_listener);
}

MATCHER_P2(IsRange, start, end, "") {
  return ::testing::ExplainMatchResult(
      ::testing::AllOf(::testing::Property("begin", &AdjustedByteRange::begin, start),
                       ::testing::Property("end", &AdjustedByteRange::end, end)),
      arg, result_listener);
}

TEST_F(CacheFilterTest, PassThroughIfCacheDisabled) {
  auto filter = makeFilter(nullptr);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  EXPECT_THAT(filter->encodeHeaders(response_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  // Details should not have been set by cache filter.
  EXPECT_THAT(decoder_callbacks_.details(), Eq(""));
}

TEST_F(CacheFilterTest, PassThroughIfRequestHasBody) {
  auto filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Uncacheable));
  EXPECT_THAT(filter->decodeHeaders(request_headers_, false),
              Eq(Http::FilterHeadersStatus::Continue));
  Buffer::OwnedImpl body("a");
  EXPECT_THAT(filter->decodeData(body, true), Eq(Http::FilterDataStatus::Continue));
  EXPECT_THAT(filter->encodeHeaders(response_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  // Details should not have been set by cache filter.
  EXPECT_THAT(decoder_callbacks_.details(), Eq(""));
}

TEST_F(CacheFilterTest, PassThroughIfCacheabilityIsNo) {
  auto filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Uncacheable));
  request_headers_.addCopy(Http::CustomHeaders::get().IfNoneMatch, "1");
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  EXPECT_THAT(filter->encodeHeaders(response_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  // Details should not have been set by cache filter.
  EXPECT_THAT(decoder_callbacks_.details(), Eq(""));
}

TEST_F(CacheFilterTest, NoRouteShouldLocalReply) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::NotFound, _, _, _, "cache_no_route"));
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache_no_route"));
}

TEST_F(CacheFilterTest, NoClusterShouldLocalReply) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, "cache_no_cluster"));
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache_no_cluster"));
}

TEST_F(CacheFilterTest, OverriddenClusterShouldTryThatCluster) {
  config_.set_override_upstream_cluster("overridden_cluster");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  // Validate that the specified cluster was *tried*; letting it not exist
  // to keep the test simple.
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("overridden_cluster"))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, "cache_no_cluster"));
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache_no_cluster"));
}

TEST_F(CacheFilterDeathTest, TimeoutBeforeLookupCompletesImpliesABug) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, /* auto_destroy = */ false);
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  ASSERT_THAT(captured_lookup_callback_, NotNull());
  // Validate some request fields; this can be omitted for other tests since
  // everything should be the same.
  EXPECT_THAT(captured_lookup_request_->key().host(), Eq("fake_host"));
  EXPECT_THAT(captured_lookup_request_->requestHeaders(), IsSupersetOfHeaders(request_headers_));
  EXPECT_THAT(&captured_lookup_request_->dispatcher(), Eq(dispatcher_.get()));

  response_headers_.setStatus(absl::StrCat(Envoy::enumToInt(Http::Code::RequestTimeout)));
  EXPECT_ENVOY_BUG(filter->encodeHeaders(response_headers_, true),
                   "Request timed out while cache lookup was outstanding.");
}

TEST_F(CacheFilterTest, EncodeHeadersBeforeLookupCompletesAbortsTheLookupCallback) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  ASSERT_THAT(captured_lookup_callback_, NotNull());
  EXPECT_THAT(filter->encodeHeaders(response_headers_, true),
              Eq(Http::FilterHeadersStatus::Continue));
  // A null lookup result is disallowed; encodeHeaders being called before it
  // completes should have cancelled the callback, so calling it now with invalid
  // data proves the cancellation has taken effect.
  captured_lookup_callback_(nullptr);
  // Since filter was aborted it should not have set response code details.
  EXPECT_THAT(decoder_callbacks_.details(), Eq(""));
}

TEST_F(CacheFilterTest, FilterDestroyedBeforeLookupCompletesAbortsTheLookupCallback) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  ASSERT_THAT(captured_lookup_callback_, NotNull());
  filter.reset();
  // Callback with nullptr would be invalid *and* would be operating on a
  // now-defunct filter pointer - so calling it proves it was cancelled.
  captured_lookup_callback_(nullptr);
}

TEST_F(CacheFilterTest, ResetDuringLookupResetsDownstream) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(decoder_callbacks_, resetStream);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{nullptr, CacheEntryStatus::LookupError}));
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.aborted_lookup"));
}

TEST_F(CacheFilterTest, ResetDuringGetHeadersResetsDownstream) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  EXPECT_CALL(decoder_callbacks_, resetStream);
  captured_get_headers_callback_(nullptr, EndStream::Reset);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.aborted_headers"));
}

TEST_F(CacheFilterTest, GetHeadersWithHeadersOnlyResponseCompletes) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::End);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.insert_via_upstream"));
}

TEST_F(CacheFilterTest, PartialContentCodeWithNoContentRangeGivesFullContent) {
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, Gt(500)), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, PartialContentCodeWithInvalidContentRangeGivesFullContent) {
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "invalid-value");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, Gt(500)), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, PartialContentCodeWithInvalidContentRangeNumberGivesFullContent) {
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes */invalid");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, Gt(500)), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, PartialContentCodeWithWildContentRangeUsesSize) {
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes */100");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, 100), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, PartialContentCodeWithInvalidRangeElementsDefaultsToZeroAndMax) {
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes invalid-invalid/100");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, Gt(500)), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, DestroyedDuringEncodeHeadersPreventsGetBody) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false))
      .WillOnce([&filter](Http::ResponseHeaderMap&, bool) { filter->onDestroy(); });
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
}

TEST_F(CacheFilterTest, ResetDuringGetBodyResetsDownstream) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  EXPECT_CALL(decoder_callbacks_, resetStream);
  captured_get_body_callback_(nullptr, EndStream::Reset);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.aborted_body"));
}

TEST_F(CacheFilterTest, GetBodyAdvancesRequestRange) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Miss));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(0), _));
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(5), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Miss}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(" world!"), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>(" world!"), EndStream::End);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.insert_via_upstream"));
}

TEST_F(CacheFilterTest, GetBodyReturningNullBufferAndEndStreamCompletes) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(0), _));
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(5), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(""), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  captured_get_body_callback_(nullptr, EndStream::End);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterTest, GetBodyReturningNullBufferAndNoEndStreamGoesOnToTrailers) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(0), _));
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(5), _));
  EXPECT_CALL(*mock_http_source_, getTrailers);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), false));
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(IsSupersetOfHeaders(response_trailers_)));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  captured_get_body_callback_(nullptr, EndStream::More);
  captured_get_trailers_callback_(createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers_),
                                  EndStream::End);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterDeathTest, GetBodyReturningBufferLargerThanRequestedIsABug) {
  request_headers_.addCopy("range", "bytes=0-5");
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes 0-5/12");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, 6), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  EXPECT_ENVOY_BUG(captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello world!"),
                                               EndStream::End),
                   "Received oversized body from http source.");
}

TEST_F(CacheFilterTest, EndOfRequestedRangeEndsStreamWhenUpstreamDoesNot) {
  request_headers_.addCopy("range", "bytes=0-4");
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes 0-4/12");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(0, 5), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterTest, EndOfRequestedRangeEndsTraileredStreamWithoutSendingTrailers) {
  request_headers_.addCopy("range", "bytes=8-11");
  response_headers_.setStatus(std::to_string(enumToInt(Http::Code::PartialContent)));
  response_headers_.addCopy("content-range", "bytes 8-11/12");
  CacheFilterSharedPtr filter = makeFilter(mock_cache_);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(IsRange(8, 12), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("beep"), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  // More here, at the end of the source data, indicates that trailers exist.
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("beep"), EndStream::More);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterTest, FilterDestroyedDuringEncodeDataPreventsFurtherRequests) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), false))
      .WillOnce([&filter]() { filter->onDestroy(); });
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  // Destruction of filter should prevent "more" from being requested.
}

TEST_F(CacheFilterTest, WatermarkDelaysUpstreamRequestingMore) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(0), _));
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(5), _));
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("hello"), false));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  filter->onAboveWriteBufferHighWatermark();
  // Move captured_get_body_callback_ into another variable so that it being
  // nullptr can be used to ensure the second callback is not in flight.
  auto cb = std::move(captured_get_body_callback_);
  captured_get_body_callback_ = nullptr;
  cb(std::make_unique<Buffer::OwnedImpl>("hello"), EndStream::More);
  // A new callback should not be in flight because of the watermark.
  EXPECT_THAT(captured_get_body_callback_, IsNull());
  // Watermark deeper!
  filter->onAboveWriteBufferHighWatermark();
  // Unwatermarking one level should not release the request.
  filter->onBelowWriteBufferLowWatermark();
  EXPECT_THAT(captured_get_body_callback_, IsNull());
  // Unwatermarking back to zero should release the request.
  filter->onBelowWriteBufferLowWatermark();
  EXPECT_THAT(captured_get_body_callback_, NotNull());
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("world"), true));
  captured_get_body_callback_(std::make_unique<Buffer::OwnedImpl>("world"), EndStream::End);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterTest, DeepRecursionOfGetBodyDoesntOverflowStack) {
  // Since it's possible for a cache to call back with body data instantly without
  // posting it to a dispatcher, we want to be sure that the implementation
  // doesn't cause a buffer overflow if that happens *a lot*.
  uint64_t depth = 0;
  uint64_t max_depth = 60000;
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody)
      .WillRepeatedly([&depth, &max_depth](AdjustedByteRange range, GetBodyCallback&& cb) {
        ASSERT_THAT(range.begin(), Eq(depth));
        if (++depth < max_depth) {
          return cb(std::make_unique<Buffer::OwnedImpl>("a"), EndStream::More);
        } else {
          return cb(std::make_unique<Buffer::OwnedImpl>("a"), EndStream::End);
        }
      });
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("a"), false)).Times(max_depth - 1);
  EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual("a"), true));
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.response_from_cache_filter"));
}

TEST_F(CacheFilterTest, FilterDestroyedDuringEncodeTrailersPreventsFurtherAction) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody);
  EXPECT_CALL(*mock_http_source_, getTrailers);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(IsSupersetOfHeaders(response_trailers_)))
      .WillOnce([&filter]() { filter->onDestroy(); });
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(nullptr, EndStream::More);
  captured_get_trailers_callback_(createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers_),
                                  EndStream::End);
  // Destruction of filter should prevent finalizeEncodingCachedResponse, but
  // that's undetectable right now because it doesn't do anything anyway.
}

TEST_F(CacheFilterTest, FilterResetDuringEncodeTrailersResetsDownstream) {
  CacheFilterSharedPtr filter = makeFilter(mock_cache_, false);
  EXPECT_CALL(stats(), incForStatus(CacheEntryStatus::Hit));
  EXPECT_CALL(*mock_cache_, lookup);
  EXPECT_THAT(filter->decodeHeaders(request_headers_, true),
              Eq(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*mock_http_source_, getHeaders);
  EXPECT_CALL(*mock_http_source_, getBody(RangeStartsWith(0), _));
  EXPECT_CALL(*mock_http_source_, getTrailers);
  captured_lookup_callback_(std::make_unique<ActiveLookupResult>(
      ActiveLookupResult{std::move(mock_http_source_), CacheEntryStatus::Hit}));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
  EXPECT_CALL(decoder_callbacks_, resetStream);
  captured_get_headers_callback_(createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_),
                                 EndStream::More);
  captured_get_body_callback_(nullptr, EndStream::More);
  captured_get_trailers_callback_(nullptr, EndStream::Reset);
  EXPECT_THAT(decoder_callbacks_.details(), Eq("cache.aborted_trailers"));
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
