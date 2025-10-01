#include "source/extensions/filters/http/cache_v2/upstream_request_impl.h"

#include "test/extensions/filters/http/cache_v2/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

using testing::_;
using testing::IsNull;
using testing::MockFunction;
using testing::Pointee;

class UpstreamRequestTest : public ::testing::Test {
protected:
  // Arbitrary buffer limit for testing.
  virtual int bufferLimit() const { return 1024; }
  void SetUp() override {
    EXPECT_CALL(async_client_, start(_, _))
        .WillOnce([this](Http::AsyncClient::StreamCallbacks& callbacks,
                         const Http::AsyncClient::StreamOptions&) {
          http_callbacks_ = &callbacks;
          return &http_stream_;
        });
    EXPECT_CALL(http_stream_, sendHeaders(HeaderMapEqualRef(&request_headers_), true));
    Http::AsyncClient::StreamOptions options;
    options.setBufferLimit(bufferLimit());
    EXPECT_CALL(dispatcher_, isThreadSafe())
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(true));
    upstream_request_ =
        UpstreamRequestImplFactory(dispatcher_, async_client_, options).create(stats_provider_);
    upstream_request_->sendHeaders(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(request_headers_));
  }

protected:
  Event::MockDispatcher dispatcher_;
  Http::AsyncClient::StreamCallbacks* http_callbacks_;
  Http::MockAsyncClientStream http_stream_;
  Http::MockAsyncClient async_client_;
  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/banana"}};
  std::shared_ptr<MockCacheFilterStatsProvider> stats_provider_ =
      std::make_shared<testing::NiceMock<MockCacheFilterStatsProvider>>();
  UpstreamRequestPtr upstream_request_;
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers_{{"x", "y"}};
};

TEST_F(UpstreamRequestTest, ResetBeforeHeadersRequestedDeliversResetToCallback) {
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_cb;
  http_callbacks_->onReset();
  EXPECT_CALL(header_cb, Call(IsNull(), EndStream::Reset));
  upstream_request_->getHeaders(header_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, ResetBeforeHeadersArrivedDeliversResetToCallback) {
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_cb;
  upstream_request_->getHeaders(header_cb.AsStdFunction());
  EXPECT_CALL(header_cb, Call(IsNull(), EndStream::Reset));
  http_callbacks_->onReset();
}

TEST_F(UpstreamRequestTest, HeadersArrivedThenRequestedDeliversHeaders) {
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_cb;
  http_callbacks_->onHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                             false);
  EXPECT_CALL(header_cb, Call(HeaderMapEqualIgnoreOrder(&response_headers_), EndStream::More));
  upstream_request_->getHeaders(header_cb.AsStdFunction());
  EXPECT_CALL(http_stream_, reset());
}

TEST_F(UpstreamRequestTest, HeadersRequestedThenArrivedDeliversHeaders) {
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_cb;
  upstream_request_->getHeaders(header_cb.AsStdFunction());
  EXPECT_CALL(header_cb, Call(HeaderMapEqualIgnoreOrder(&response_headers_), EndStream::More));
  http_callbacks_->onHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                             false);
  EXPECT_CALL(http_stream_, reset());
}

TEST_F(UpstreamRequestTest, HeadersEndStreamWorksAndPreventsReset) {
  MockFunction<void(Http::ResponseHeaderMapPtr, EndStream)> header_cb;
  upstream_request_->getHeaders(header_cb.AsStdFunction());
  EXPECT_CALL(header_cb, Call(HeaderMapEqualIgnoreOrder(&response_headers_), EndStream::End));
  http_callbacks_->onHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                             true);
  http_callbacks_->onComplete();
}

TEST_F(UpstreamRequestTest, ResetBeforeBodyRequestedDeliversResetToCallback) {
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  http_callbacks_->onReset();
  EXPECT_CALL(body_cb, Call(IsNull(), EndStream::Reset));
  upstream_request_->getBody(AdjustedByteRange{0, 5}, body_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, ResetAfterBodyRequestedDeliversResetToCallback) {
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  upstream_request_->getBody(AdjustedByteRange{0, 5}, body_cb.AsStdFunction());
  EXPECT_CALL(body_cb, Call(IsNull(), EndStream::Reset));
  http_callbacks_->onReset();
}

TEST_F(UpstreamRequestTest, BodyRequestedThenArrivedDeliversBody) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  upstream_request_->getBody(AdjustedByteRange{0, 5}, body_cb.AsStdFunction());
  EXPECT_CALL(body_cb, Call(Pointee(BufferStringEqual("hello")), EndStream::End));
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
}

TEST_F(UpstreamRequestTest, BodyArrivedThenOversizedRequestedDeliversBody) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb, Call(Pointee(BufferStringEqual("hello")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{0, 99}, body_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, BodyArrivedThenRequestedInPiecesDeliversBody) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb1;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb2;
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb1, Call(Pointee(BufferStringEqual("hel")), EndStream::More));
  upstream_request_->getBody(AdjustedByteRange{0, 3}, body_cb1.AsStdFunction());
  EXPECT_CALL(body_cb2, Call(Pointee(BufferStringEqual("lo")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{3, 5}, body_cb2.AsStdFunction());
}

TEST_F(UpstreamRequestTest, BodyAlternatingActionsDeliversBody) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb1;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb2;
  upstream_request_->getBody(AdjustedByteRange{0, 3}, body_cb1.AsStdFunction());
  EXPECT_CALL(body_cb1, Call(Pointee(BufferStringEqual("hel")), EndStream::More));
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb2, Call(Pointee(BufferStringEqual("lo")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{3, 5}, body_cb2.AsStdFunction());
}

TEST_F(UpstreamRequestTest, BodyInMultiplePiecesDeliversBody) {
  Buffer::OwnedImpl data1{"hello"};
  Buffer::OwnedImpl data2{"there"};
  Buffer::OwnedImpl data3{"banana"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb1;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb2;
  upstream_request_->getBody(AdjustedByteRange{0, 99}, body_cb1.AsStdFunction());
  EXPECT_CALL(body_cb1, Call(Pointee(BufferStringEqual("hello")), EndStream::More));
  http_callbacks_->onData(data1, false);
  http_callbacks_->onData(data2, false);
  http_callbacks_->onData(data3, true);
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb2, Call(Pointee(BufferStringEqual("therebanana")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{5, 99}, body_cb2.AsStdFunction());
}

TEST_F(UpstreamRequestTest, DeletionWhileBodyCallbackInFlightCallsReset) {
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  upstream_request_->getBody(AdjustedByteRange{0, 99}, body_cb.AsStdFunction());
  EXPECT_CALL(http_stream_, reset());
}

TEST_F(UpstreamRequestTest, RequestingMoreBodyAfterCompletionReturnsNull) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb1;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb2;
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb1, Call(Pointee(BufferStringEqual("hello")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{0, 99}, body_cb1.AsStdFunction());
  EXPECT_CALL(body_cb2, Call(IsNull(), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{5, 99}, body_cb2.AsStdFunction());
}

TEST_F(UpstreamRequestTest, RequestingMoreBodyAfterTrailersResumesAndEventuallyReturnsNull) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb1;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb2;
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb3;
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailers_cb;
  http_callbacks_->onData(data, false);
  http_callbacks_->onTrailers(
      std::make_unique<Http::TestResponseTrailerMapImpl>(response_trailers_));
  http_callbacks_->onComplete();
  EXPECT_CALL(body_cb1, Call(Pointee(BufferStringEqual("hel")), EndStream::More));
  upstream_request_->getBody(AdjustedByteRange{0, 3}, body_cb1.AsStdFunction());
  EXPECT_CALL(body_cb2, Call(Pointee(BufferStringEqual("lo")), EndStream::More));
  upstream_request_->getBody(AdjustedByteRange{3, 99}, body_cb2.AsStdFunction());
  EXPECT_CALL(body_cb3, Call(IsNull(), EndStream::More));
  upstream_request_->getBody(AdjustedByteRange{5, 99}, body_cb3.AsStdFunction());
  EXPECT_CALL(trailers_cb, Call(HeaderMapEqualIgnoreOrder(&response_trailers_), EndStream::End));
  upstream_request_->getTrailers(trailers_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, ResetBeforeTrailersRequestedDeliversResetToCallback) {
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_cb;
  http_callbacks_->onReset();
  EXPECT_CALL(trailer_cb, Call(IsNull(), EndStream::Reset));
  upstream_request_->getTrailers(trailer_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, ResetBeforeTrailersArrivedDeliversResetToCallback) {
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_cb;
  upstream_request_->getTrailers(trailer_cb.AsStdFunction());
  EXPECT_CALL(trailer_cb, Call(IsNull(), EndStream::Reset));
  http_callbacks_->onReset();
}

TEST_F(UpstreamRequestTest, TrailersArrivedThenRequestedDeliversTrailers) {
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_cb;
  http_callbacks_->onTrailers(
      std::make_unique<Http::TestResponseTrailerMapImpl>(response_trailers_));
  http_callbacks_->onComplete();
  EXPECT_CALL(trailer_cb, Call(HeaderMapEqualIgnoreOrder(&response_trailers_), EndStream::End));
  upstream_request_->getTrailers(trailer_cb.AsStdFunction());
}

TEST_F(UpstreamRequestTest, TrailersRequestedThenArrivedDeliversTrailers) {
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_cb;
  upstream_request_->getTrailers(trailer_cb.AsStdFunction());
  EXPECT_CALL(trailer_cb, Call(HeaderMapEqualIgnoreOrder(&response_trailers_), EndStream::End));
  http_callbacks_->onTrailers(
      std::make_unique<Http::TestResponseTrailerMapImpl>(response_trailers_));
  http_callbacks_->onComplete();
}

TEST_F(UpstreamRequestTest, TrailersArrivedWhileExpectingMoreBodyDeliversNullBodyThenTrailers) {
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  MockFunction<void(Http::ResponseTrailerMapPtr, EndStream)> trailer_cb;
  EXPECT_CALL(body_cb, Call(IsNull(), EndStream::More));
  upstream_request_->getBody(AdjustedByteRange{0, 5}, body_cb.AsStdFunction());
  http_callbacks_->onTrailers(
      std::make_unique<Http::TestResponseTrailerMapImpl>(response_trailers_));
  testing::Mock::VerifyAndClearExpectations(&body_cb);
  EXPECT_CALL(trailer_cb, Call(HeaderMapEqualIgnoreOrder(&response_trailers_), EndStream::End));
  upstream_request_->getTrailers(trailer_cb.AsStdFunction());
  http_callbacks_->onComplete();
}

TEST_F(UpstreamRequestTest, DestroyedWhileBodyBufferedCorrectsStats) {
  Buffer::OwnedImpl data{"hello"};
  EXPECT_CALL(stats_provider_->mock_stats_, addUpstreamBufferedBytes(data.length()));
  EXPECT_CALL(http_stream_, reset());
  EXPECT_CALL(stats_provider_->mock_stats_, subUpstreamBufferedBytes(data.length()));
  http_callbacks_->onData(data, true);
  upstream_request_.reset();
}

class UpstreamRequestWithRangeHeaderTest : public UpstreamRequestTest {
protected:
  void SetUp() override {
    request_headers_.addCopy("range", "bytes=3-4");
    UpstreamRequestTest::SetUp();
  }
};

TEST_F(UpstreamRequestWithRangeHeaderTest, RangeHeaderSkipsToExpectedStreamPos) {
  Buffer::OwnedImpl data{"lo"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  upstream_request_->getBody(AdjustedByteRange{3, 5}, body_cb.AsStdFunction());
  EXPECT_CALL(body_cb, Call(Pointee(BufferStringEqual("lo")), EndStream::End));
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
}

class UpstreamRequestWithSmallBuffersTest : public UpstreamRequestTest {
protected:
  int bufferLimit() const override { return 3; }
};

TEST_F(UpstreamRequestWithSmallBuffersTest, WatermarksPauseTheUpstream) {
  Buffer::OwnedImpl data{"hello"};
  MockFunction<void(Buffer::InstancePtr, EndStream)> body_cb;
  // TODO(ravenblack): validate that onAboveHighWatermark actions
  // are performed during onData, once it's possible to pause flow
  // from upstream.
  http_callbacks_->onData(data, true);
  http_callbacks_->onComplete();
  // TODO(ravenblack): validate that onBelowHighWatermark actions
  // are performed during onData, once it's possible to pause flow
  // from upstream.
  EXPECT_CALL(body_cb, Call(Pointee(BufferStringEqual("hello")), EndStream::End));
  upstream_request_->getBody(AdjustedByteRange{0, 5}, body_cb.AsStdFunction());
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
