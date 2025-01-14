#include "source/extensions/filters/http/cache/upstream_request_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

using testing::_;
using testing::IsNull;
using testing::MockFunction;
using testing::Pointee;

class UpstreamRequestTest : public ::testing::Test {
  void SetUp() override {
    EXPECT_CALL(async_client_, start(_, _))
        .WillOnce([this](Http::AsyncClient::StreamCallbacks& callbacks,
                         const Http::AsyncClient::StreamOptions&) {
          http_callbacks_ = &callbacks;
          return &http_stream_;
        });
    EXPECT_CALL(http_stream_, sendHeaders(HeaderMapEqualRef(&request_headers_), true));
    Http::AsyncClient::StreamOptions options;
    options.setBufferLimit(1024);
    Event::MockDispatcher dispatcher;
    EXPECT_CALL(dispatcher, post).WillOnce([](Event::PostCb cb) { cb(); });
    upstream_request_ =
        UpstreamRequestImplFactory(dispatcher, async_client_, options).create(request_headers_);
  }

protected:
  Http::AsyncClient::StreamCallbacks* http_callbacks_;
  Http::MockAsyncClientStream http_stream_;
  Http::MockAsyncClient async_client_;
  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/banana"}};
  HttpSourcePtr upstream_request_;
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

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
