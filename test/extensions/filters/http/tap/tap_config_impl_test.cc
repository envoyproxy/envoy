#include "extensions/filters/http/tap/tap_config_impl.h"

#include "test/extensions/common/tap/common.h"
#include "test/extensions/filters/http/tap/common.h"
#include "test/mocks/common.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::Assign;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {
namespace {

namespace TapCommon = Extensions::Common::Tap;

class HttpPerRequestTapperImplTest : public testing::Test {
public:
  HttpPerRequestTapperImplTest() {
    EXPECT_CALL(*config_, createPerTapSinkHandleManager_(1)).WillOnce(Return(sink_manager_));
    EXPECT_CALL(*config_, createMatchStatusVector())
        .WillOnce(Return(TapCommon::Matcher::MatchStatusVector(1)));
    EXPECT_CALL(*config_, rootMatcher()).WillRepeatedly(ReturnRef(matcher_));
    EXPECT_CALL(matcher_, onNewStream(_)).WillOnce(SaveArgAddress(&statuses_));
    tapper_ = std::make_unique<HttpPerRequestTapperImpl>(config_, 1);
  }

  std::shared_ptr<MockHttpTapConfig> config_{std::make_shared<MockHttpTapConfig>()};
  // Raw pointer, returned via mock to unique_ptr.
  TapCommon::MockPerTapSinkHandleManager* sink_manager_ =
      new TapCommon::MockPerTapSinkHandleManager;
  std::unique_ptr<HttpPerRequestTapperImpl> tapper_;
  std::vector<TapCommon::MatcherPtr> matchers_{1};
  TapCommon::MockMatcher matcher_{matchers_};
  TapCommon::Matcher::MatchStatusVector* statuses_;
  const Http::TestRequestHeaderMapImpl request_headers_{{"a", "b"}};
  const Http::TestRequestTrailerMapImpl request_trailers_{{"c", "d"}};
  const Http::TestResponseHeaderMapImpl response_headers_{{"e", "f"}};
  const Http::TestResponseTrailerMapImpl response_trailers_{{"g", "h"}};
};

// Buffered tap with no match.
TEST_F(HttpPerRequestTapperImplTest, BufferedFlowNoTap) {
  EXPECT_CALL(*config_, streaming()).WillRepeatedly(Return(false));
  EXPECT_CALL(*config_, maxBufferedRxBytes()).WillRepeatedly(Return(1024));
  EXPECT_CALL(*config_, maxBufferedTxBytes()).WillRepeatedly(Return(1024));

  InSequence s;
  EXPECT_CALL(matcher_, onHttpRequestHeaders(_, _));
  tapper_->onRequestHeaders(request_headers_);
  tapper_->onRequestBody(Buffer::OwnedImpl("hello"));
  EXPECT_CALL(matcher_, onHttpRequestTrailers(_, _));
  tapper_->onRequestTrailers(request_trailers_);
  EXPECT_CALL(matcher_, onHttpResponseHeaders(_, _));
  tapper_->onResponseHeaders(response_headers_);
  tapper_->onResponseBody(Buffer::OwnedImpl("world"));
  EXPECT_CALL(matcher_, onHttpResponseTrailers(_, _));
  tapper_->onResponseTrailers(response_trailers_);
  EXPECT_FALSE(tapper_->onDestroyLog());
}

// Buffered tap with a match.
TEST_F(HttpPerRequestTapperImplTest, BufferedFlowTap) {
  EXPECT_CALL(*config_, streaming()).WillRepeatedly(Return(false));
  EXPECT_CALL(*config_, maxBufferedRxBytes()).WillRepeatedly(Return(1024));
  EXPECT_CALL(*config_, maxBufferedTxBytes()).WillRepeatedly(Return(1024));

  InSequence s;
  EXPECT_CALL(matcher_, onHttpRequestHeaders(_, _));
  tapper_->onRequestHeaders(request_headers_);
  tapper_->onRequestBody(Buffer::OwnedImpl("hello"));
  EXPECT_CALL(matcher_, onHttpRequestTrailers(_, _));
  tapper_->onRequestTrailers(request_trailers_);
  EXPECT_CALL(matcher_, onHttpResponseHeaders(_, _));
  tapper_->onResponseHeaders(response_headers_);
  tapper_->onResponseBody(Buffer::OwnedImpl("world"));
  EXPECT_CALL(matcher_, onHttpResponseTrailers(_, _));
  tapper_->onResponseTrailers(response_trailers_);
  (*statuses_)[0].matches_ = true;
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_buffered_trace:
  request:
    headers:
      - key: a
        value: b
    body:
      as_bytes: aGVsbG8=
    trailers:
      - key: c
        value: d
  response:
    headers:
      - key: e
        value: f
    body:
      as_bytes: d29ybGQ=
    trailers:
      - key: g
        value: h
)EOF")));
  EXPECT_TRUE(tapper_->onDestroyLog());
}

// Streamed tap where we match on request trailers and have to flush request headers/body.
TEST_F(HttpPerRequestTapperImplTest, StreamedMatchRequestTrailers) {
  EXPECT_CALL(*config_, streaming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*config_, maxBufferedRxBytes()).WillRepeatedly(Return(1024));
  EXPECT_CALL(*config_, maxBufferedTxBytes()).WillRepeatedly(Return(1024));

  InSequence s;
  EXPECT_CALL(matcher_, onHttpRequestHeaders(_, _));
  tapper_->onRequestHeaders(request_headers_);
  tapper_->onRequestBody(Buffer::OwnedImpl("hello"));
  EXPECT_CALL(matcher_, onHttpRequestTrailers(_, _))
      .WillOnce(Assign(&(*statuses_)[0].matches_, true));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_headers:
    headers:
      - key: a
        value: b
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_body_chunk:
    as_bytes: aGVsbG8=
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_trailers:
    headers:
      - key: c
        value: d
)EOF")));
  tapper_->onRequestTrailers(request_trailers_);
  EXPECT_CALL(matcher_, onHttpResponseHeaders(_, _));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_headers:
    headers:
      - key: e
        value: f
)EOF")));
  tapper_->onResponseHeaders(response_headers_);
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_body_chunk:
    as_bytes: d29ybGQ=
)EOF")));
  tapper_->onResponseBody(Buffer::OwnedImpl("world"));
  EXPECT_CALL(matcher_, onHttpResponseTrailers(_, _));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_trailers:
    headers:
      - key: g
        value: h
)EOF")));
  tapper_->onResponseTrailers(response_trailers_);
  EXPECT_TRUE(tapper_->onDestroyLog());
}

// Streamed tap where we match on response trailers and have to flush everything.
TEST_F(HttpPerRequestTapperImplTest, StreamedMatchResponseTrailers) {
  EXPECT_CALL(*config_, streaming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*config_, maxBufferedRxBytes()).WillRepeatedly(Return(1024));
  EXPECT_CALL(*config_, maxBufferedTxBytes()).WillRepeatedly(Return(1024));

  InSequence s;
  EXPECT_CALL(matcher_, onHttpRequestHeaders(_, _));
  tapper_->onRequestHeaders(request_headers_);
  tapper_->onRequestBody(Buffer::OwnedImpl("hello"));
  EXPECT_CALL(matcher_, onHttpRequestTrailers(_, _));
  tapper_->onRequestTrailers(request_trailers_);
  EXPECT_CALL(matcher_, onHttpResponseHeaders(_, _));
  tapper_->onResponseHeaders(response_headers_);
  tapper_->onResponseBody(Buffer::OwnedImpl("world"));
  EXPECT_CALL(matcher_, onHttpResponseTrailers(_, _))
      .WillOnce(Assign(&(*statuses_)[0].matches_, true));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_headers:
    headers:
      - key: a
        value: b
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_body_chunk:
    as_bytes: aGVsbG8=
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  request_trailers:
    headers:
      - key: c
        value: d
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_headers:
    headers:
      - key: e
        value: f
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_body_chunk:
    as_bytes: d29ybGQ=
)EOF")));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
http_streamed_trace_segment:
  trace_id: 1
  response_trailers:
    headers:
      - key: g
        value: h
)EOF")));
  tapper_->onResponseTrailers(response_trailers_);
  EXPECT_TRUE(tapper_->onDestroyLog());
}

} // namespace
} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
