#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::AtLeast;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  setup(false, "envoy-server-test");
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest();

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
      }));
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, DisconnectOnProxyConnectionDisconnect) {
  setup(false, "envoy-server-test");

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  startRequest();

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("close", headers.getConnectionValue());
        EXPECT_EQ(nullptr, headers.ProxyConnection());
      }));
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, ResponseStartBeforeRequestComplete) {
  setup(false, "");

  // This is like ResponseBeforeRequestComplete, but it tests the case where we start the reply
  // before the request completes, but don't finish the reply until after the request completes.
  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Start the request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);

  // Start the response
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("", headers.getServerValue());
      }));
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  // Finish the request.
  EXPECT_CALL(*filter, decodeData(_, true));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_->decodeData(data, true);
    return Http::okStatus();
  }));

  conn_manager_->onData(fake_input, false);

  // Since we started the response before the request was complete, we will still close the
  // connection since we already sent a connection: close header. We won't "reset" the stream
  // however.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
  Buffer::OwnedImpl fake_response("world");
  filter->callbacks_->encodeData(fake_response, true);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamDisconnect) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    data.drain(2);
    return Http::okStatus();
  }));

  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Now raise a remote disconnection, we should see the filter get reset called.
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamProtocolError) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return codecProtocolError("protocol error");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // A protocol exception should result in reset of the streams followed by a remote or local close
  // depending on whether the downstream client closes the connection prior to the delayed close
  // timer firing.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamProtocolErrorAccessLog) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};
  setup(false, "");

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_FALSE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError));
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return codecProtocolError("protocol error");
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamProtocolErrorAfterHeadersAccessLog) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_FALSE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError));
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
    decoder_->decodeHeaders(std::move(headers), true);

    return codecProtocolError("protocol error");
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Verify that FrameFloodException causes connection to be closed abortively.
TEST_F(HttpConnectionManagerImplTest, FrameFloodError) {
  std::shared_ptr<AccessLog::MockInstance> log_handler =
      std::make_shared<NiceMock<AccessLog::MockInstance>>();
  access_logs_ = {log_handler};
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return bufferFloodError("too many outbound frames");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // FrameFloodException should result in reset of the streams followed by abortive close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  EXPECT_CALL(*log_handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
        EXPECT_EQ("codec_error:too_many_outbound_frames",
                  stream_info.responseCodeDetails().value());
      }));
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_LOG_NOT_CONTAINS("warning", "downstream HTTP flood",
                          conn_manager_->onData(fake_input, false));

  EXPECT_TRUE(filter_callbacks_.connection_.streamInfo().hasResponseFlag(
      StreamInfo::ResponseFlag::DownstreamProtocolError));
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeoutNoCodec) {
  // Not used in the test.
  delete codec_;

  idle_timeout_ = (std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup(false, "");

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeout) {
  idle_timeout_ = (std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup(false, "");

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  startRequest(true, "hello");

  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  idle_timer->invokeCallback();

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDurationNoCodec) {
  // Not used in the test.
  delete codec_;

  max_connection_duration_ = (std::chrono::milliseconds(10));
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup(false, "");

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*connection_duration_timer, disableTimer());

  connection_duration_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDuration) {
  max_connection_duration_ = (std::chrono::milliseconds(10));
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup(false, "");

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  // Kick off the incoming data.
  startRequest(true, "hello");

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  connection_duration_timer->invokeCallback();

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
  EXPECT_CALL(*connection_duration_timer, disableTimer());
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());
}

TEST_F(HttpConnectionManagerImplTest, IntermediateBufferingEarlyResponse) {
  setup(false, "");

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the request.
  startRequest(true, "hello");

  // Mimic a decoder filter that trapped data and now sends on the headers.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        // Now filter 2 will send a complete response.
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
        decoder_filters_[1]->callbacks_->encodeHeaders(std::move(response_headers), true,
                                                       "details");
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();

  // Response is already complete so we drop buffered body data when we continue.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, _)).Times(0);
  decoder_filters_[0]->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, DoubleBuffering) {
  setup(false, "");
  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_data_copy("hello");
  startRequest(true, "hello");

  // Continue iteration and stop and buffer on the 2nd filter.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Continue iteration. We expect the 3rd filter to not receive double data but for the buffered
  // data to have been kept inline as it moves through.
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[2], decodeData(BufferEqual(&fake_data_copy), true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());
  decoder_filters_[1]->callbacks_->continueDecoding();

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, ZeroByteDataFiltering) {
  setup(false, "");
  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  startRequest();

  // Continue headers only of filter 1.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Stop zero byte data.
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  Buffer::OwnedImpl zero;
  decoder_->decodeData(zero, true);

  // Continue.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddTrailersInTrailersCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder_->decodeData(fake_data, false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"bazzz", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  setupFilterChain(2, 2);

  Http::LowerCaseString trailer_key("foo");
  std::string trailers_data("trailers");
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Invoke([&](Http::HeaderMap& trailers) -> FilterTrailersStatus {
        Http::LowerCaseString key("foo");
        EXPECT_TRUE(trailers.get(key).empty());
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // set up encodeHeaders expectations
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  // invoke encodeHeaders
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  // set up encodeData expectations
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));

  // invoke encodeData
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, false);
  // set up encodeTrailer expectations
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(Invoke([&](Http::HeaderMap& trailers) -> FilterTrailersStatus {
        // assert that the trailers set in the previous filter was ignored
        Http::LowerCaseString key("foo");
        EXPECT_TRUE(trailers.get(key).empty());
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  // invoke encodeTrailers
  decoder_filters_[0]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

TEST_F(HttpConnectionManagerImplTest, FilterAddTrailersInDataCallbackNoTrailers) {
  setup(false, "");
  setupFilterChain(2, 2);

  std::string trailers_data("trailers");
  Http::LowerCaseString trailer_key("foo");
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterDataStatus {
        decoder_filters_[0]->callbacks_->addDecodedTrailers().addCopy(trailer_key, trailers_data);
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // ensure that the second decodeData call sees end_stream = false
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));

  // since we added trailers, we should see decodeTrailers
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_)).WillOnce(Invoke([&](HeaderMap& trailers) {
    // ensure that we see the trailers set in decodeData
    Http::LowerCaseString key("foo");
    auto t = trailers.get(key);
    ASSERT(!t.empty());
    EXPECT_EQ(t[0]->value(), trailers_data.c_str());
    return FilterTrailersStatus::Continue;
  }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the request.
  startRequest(true, "hello");

  // set up encodeHeaders expectations
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  // invoke encodeHeaders
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  // set up encodeData expectations
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterDataStatus {
        encoder_filters_[1]->callbacks_->addEncodedTrailers().addCopy(trailer_key, trailers_data);
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  // ensure encodeData calls after setting header sees end_stream = false
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));

  EXPECT_CALL(response_encoder_, encodeData(_, false));

  // since we added trailers, we should see encodeTrailer callbacks
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_)).WillOnce(Invoke([&](HeaderMap& trailers) {
    // ensure that we see the trailers set in decodeData
    Http::LowerCaseString key("foo");
    auto t = trailers.get(key);
    EXPECT_EQ(t[0]->value(), trailers_data.c_str());
    return FilterTrailersStatus::Continue;
  }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());

  // Ensure that we call encodeTrailers
  EXPECT_CALL(response_encoder_, encodeTrailers(_));

  expectOnDestroy();
  // invoke encodeData
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder_->decodeData(fake_data, false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  Buffer::OwnedImpl trailers_data("hello");
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        decoder_filters_[0]->callbacks_->addDecodedData(trailers_data, true);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(Ref(trailers_data), false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));

  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, false);
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        encoder_filters_[1]->callbacks_->addEncodedData(trailers_data, true);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeData(Ref(trailers_data), false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

// Don't send data frames, only headers and trailers.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback_NoDataFrames) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  setupFilterChain(2, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl trailers_data("hello");
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        decoder_filters_[0]->callbacks_->addDecodedData(trailers_data, false);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        encoder_filters_[0]->callbacks_->addEncodedData(trailers_data, false);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  decoder_filters_[0]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

// Don't send data frames, only headers and trailers.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback_ContinueAfterCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  setupFilterChain(2, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl trailers_data("hello");
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        decoder_filters_[0]->callbacks_->addDecodedData(trailers_data, false);
        return FilterTrailersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  decoder_filters_[0]->callbacks_->continueDecoding();

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        encoder_filters_[0]->callbacks_->addEncodedData(trailers_data, false);
        return FilterTrailersStatus::StopIteration;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());

  decoder_filters_[0]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  encoder_filters_[0]->callbacks_->continueEncoding();
}

// Add*Data during the *Data callbacks.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyDuringDecodeData) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl data1("hello");
    decoder_->decodeData(data1, false);

    Buffer::OwnedImpl data2("world");
    decoder_->decodeData(data2, true);
    return Http::okStatus();
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        decoder_filters_[0]->callbacks_->addDecodedData(data, true);
        EXPECT_EQ(decoder_filters_[0]->callbacks_->decodingBuffer()->toString(), "helloworld");
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        encoder_filters_[1]->callbacks_->addEncodedData(data, true);
        EXPECT_EQ(encoder_filters_[1]->callbacks_->encodingBuffer()->toString(), "goodbye");
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");
  Buffer::OwnedImpl data1("good");
  decoder_filters_[1]->callbacks_->encodeData(data1, false);
  Buffer::OwnedImpl data2("bye");
  decoder_filters_[1]->callbacks_->encodeData(data2, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInline) {
  setup(false, "");
  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        decoder_filters_[0]->callbacks_->addDecodedData(data, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  startRequest(true);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        encoder_filters_[1]->callbacks_->addEncodedData(data, true);
        EXPECT_EQ(5UL, encoder_filters_[0]->callbacks_->encodingBuffer()->length());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
}

TEST_F(HttpConnectionManagerImplTest, Filter) {
  setup(false, "");

  setupFilterChain(3, 2);
  const std::string fake_cluster1_name = "fake_cluster1";
  const std::string fake_cluster2_name = "fake_cluster2";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(_))
      .WillOnce(Return(fake_cluster1.get()))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));
  std::shared_ptr<Router::MockRoute> route2 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route2->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster2_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(route1))
      .WillOnce(Return(route2))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1->routeEntry(), decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());
        decoder_filters_[0]->callbacks_->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(route2->routeEntry(), decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
        // RDS & CDS consistency problem: route2 points to fake_cluster2, which doesn't exist.
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->clusterInfo());
        decoder_filters_[1]->callbacks_->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->clusterInfo());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->streamInfo().routeEntry());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  // Kick off the incoming data.
  startRequest(true);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, UpstreamWatermarkCallbacks) {
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Mimic the upstream connection backing up. The router would call
  // onDecoderFilterAboveWriteBufferHighWatermark which should readDisable the stream and increment
  // stats.
  EXPECT_CALL(response_encoder_, getStream()).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(true));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_paused_reading_total_.value());

  // Resume the flow of data. When the router buffer drains it calls
  // onDecoderFilterBelowWriteBufferLowWatermark which should re-enable reads on the stream.
  EXPECT_CALL(response_encoder_, getStream()).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(false));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_resumed_reading_total_.value());

  // Backup upstream once again.
  EXPECT_CALL(response_encoder_, getStream()).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(true));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(2U, stats_.named_.downstream_flow_control_paused_reading_total_.value());

  // Send a full response.
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();
  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
}

TEST_F(HttpConnectionManagerImplTest, UnderlyingConnectionWatermarksPassedOnWithLazyCreation) {
  setup(false, "");

  // Make sure codec_ is created.
  EXPECT_CALL(*codec_, dispatch(_));
  Buffer::OwnedImpl fake_input("");
  conn_manager_->onData(fake_input, false);

  // Mark the connection manger as backed up before the stream is created.
  ASSERT_EQ(decoder_filters_.size(), 0);
  EXPECT_CALL(*codec_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn_manager_->onAboveWriteBufferHighWatermark();

  // Create the stream. Defer the creation of the filter chain by not sending
  // complete headers.
  {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
      decoder_ = &conn_manager_->newStream(response_encoder_);
      // Call the high buffer callbacks as the codecs do.
      stream_callbacks_->onAboveWriteBufferHighWatermark();
      return Http::okStatus();
    }));

    // Send fake data to kick off newStream being created.
    Buffer::OwnedImpl fake_input2("asdf");
    conn_manager_->onData(fake_input2, false);
  }

  // Now set up the filter chain by sending full headers. The filters should be
  // immediately appraised that the low watermark is in effect.
  {
    setupFilterChain(2, 2);
    EXPECT_CALL(filter_callbacks_.connection_, aboveHighWatermark()).Times(0);
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
      RequestHeaderMapPtr headers{
          new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
      decoder_->decodeHeaders(std::move(headers), true);
      return Http::okStatus();
    }));
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          Buffer::OwnedImpl data("hello");
          decoder_filters_[0]->callbacks_->addDecodedData(data, true);
          return FilterHeadersStatus::Continue;
        }));
    EXPECT_CALL(*decoder_filters_[0], decodeComplete());
    sendRequestHeadersAndData();
    ASSERT_GE(decoder_filters_.size(), 1);
    MockDownstreamWatermarkCallbacks callbacks;
    EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
    decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);

    // Ensures that when new callbacks are registered they get invoked immediately
    // and the already-registered callbacks do not.
    MockDownstreamWatermarkCallbacks callbacks2;
    EXPECT_CALL(callbacks2, onAboveWriteBufferHighWatermark());
    decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks2);
  }
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, UnderlyingConnectionWatermarksUnwoundWithLazyCreation) {
  setup(false, "");

  // Make sure codec_ is created.
  EXPECT_CALL(*codec_, dispatch(_));
  Buffer::OwnedImpl fake_input("");
  conn_manager_->onData(fake_input, false);

  // Mark the connection manger as backed up before the stream is created.
  ASSERT_EQ(decoder_filters_.size(), 0);
  EXPECT_CALL(*codec_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn_manager_->onAboveWriteBufferHighWatermark();

  // Create the stream. Defer the creation of the filter chain by not sending
  // complete headers.
  {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
      decoder_ = &conn_manager_->newStream(response_encoder_);
      // Call the high buffer callbacks as the codecs do.
      stream_callbacks_->onAboveWriteBufferHighWatermark();
      return Http::okStatus();
    }));

    // Send fake data to kick off newStream being created.
    Buffer::OwnedImpl fake_input2("asdf");
    conn_manager_->onData(fake_input2, false);
  }

  // Now before the filter chain is created, fire the low watermark callbacks
  // and ensure it is passed down to the stream.
  ASSERT(stream_callbacks_ != nullptr);
  EXPECT_CALL(*codec_, onUnderlyingConnectionBelowWriteBufferLowWatermark())
      .WillOnce(Invoke([&]() -> void { stream_callbacks_->onBelowWriteBufferLowWatermark(); }));
  conn_manager_->onBelowWriteBufferLowWatermark();

  // Now set up the filter chain by sending full headers. The filters should
  // not get any watermark callbacks.
  {
    setupFilterChain(2, 2);
    EXPECT_CALL(filter_callbacks_.connection_, aboveHighWatermark()).Times(0);
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
      RequestHeaderMapPtr headers{
          new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
      decoder_->decodeHeaders(std::move(headers), true);
      return Http::okStatus();
    }));
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          Buffer::OwnedImpl data("hello");
          decoder_filters_[0]->callbacks_->addDecodedData(data, true);
          return FilterHeadersStatus::Continue;
        }));
    EXPECT_CALL(*decoder_filters_[0], decodeComplete());
    sendRequestHeadersAndData();
    ASSERT_GE(decoder_filters_.size(), 1);
    MockDownstreamWatermarkCallbacks callbacks;
    EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
    decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);
  }
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, AlterFilterWatermarkLimits) {
  initial_buffer_limit_ = 100;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Check initial limits.
  EXPECT_EQ(initial_buffer_limit_, decoder_filters_[0]->callbacks_->decoderBufferLimit());
  EXPECT_EQ(initial_buffer_limit_, encoder_filters_[0]->callbacks_->encoderBufferLimit());

  // Check lowering the limits.
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit(initial_buffer_limit_ - 1);
  EXPECT_EQ(initial_buffer_limit_ - 1, decoder_filters_[0]->callbacks_->decoderBufferLimit());

  // Check raising the limits.
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit(initial_buffer_limit_ + 1);
  EXPECT_EQ(initial_buffer_limit_ + 1, decoder_filters_[0]->callbacks_->decoderBufferLimit());
  EXPECT_EQ(initial_buffer_limit_ + 1, encoder_filters_[0]->callbacks_->encoderBufferLimit());

  // Verify turning off buffer limits works.
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit(0);
  EXPECT_EQ(0, decoder_filters_[0]->callbacks_->decoderBufferLimit());

  // Once the limits are turned off can be turned on again.
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit(100);
  EXPECT_EQ(100, decoder_filters_[0]->callbacks_->decoderBufferLimit());

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, HitFilterWatermarkLimits) {
  log_handler_ = std::make_shared<NiceMock<AccessLog::MockInstance>>();

  initial_buffer_limit_ = 1;
  streaming_filter_ = true;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);

  // The filter is a streaming filter. Sending 4 bytes should hit the
  // watermark limit and disable reads on the stream.
  EXPECT_CALL(stream_, readDisable(true));
  sendRequestHeadersAndData();

  // Change the limit so the buffered data is below the new watermark. The
  // stream should be read-enabled
  EXPECT_CALL(stream_, readDisable(false));
  int buffer_len = decoder_filters_[0]->callbacks_->decodingBuffer()->length();
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit((buffer_len + 1) * 2);

  // Start the response
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  MockDownstreamWatermarkCallbacks callbacks;
  decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);
  MockDownstreamWatermarkCallbacks callbacks2;
  decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks2);

  // Now overload the buffer with response data. The downstream watermark
  // callbacks should be called.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(callbacks2, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl fake_response("A long enough string to go over watermarks");
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);

  // unregister callbacks2
  decoder_filters_[0]->callbacks_->removeDownstreamWatermarkCallbacks(callbacks2);

  // Change the limit so the buffered data is below the new watermark.
  buffer_len = encoder_filters_[1]->callbacks_->encodingBuffer()->length();
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  EXPECT_CALL(callbacks2, onBelowWriteBufferLowWatermark()).Times(0);
  encoder_filters_[1]->callbacks_->setEncoderBufferLimit((buffer_len + 1) * 2);

  EXPECT_CALL(*log_handler_, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_FALSE(stream_info.hasAnyResponseFlag());
      }));

  expectOnDestroy();
  EXPECT_CALL(stream_, removeCallbacks(_));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimits) {
  initial_buffer_limit_ = 10;
  streaming_filter_ = false;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Set the filter to be a buffering filter. Sending any data will hit the
  // watermark limit and result in a 413 being sent to the user.
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "413"}, {"content-length", "17"}, {"content-type", "text/plain"}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  Buffer::OwnedImpl data("A longer string");
  decoder_filters_[0]->callbacks_->addDecodedData(data, false);
  const auto rc_details = encoder_filters_[1]->callbacks_->streamInfo().responseCodeDetails();
  EXPECT_EQ("request_payload_too_large", rc_details.value());

  doRemoteClose();
}

// Return 413 from an intermediate filter and make sure we don't continue the filter chain.
TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimitsIntermediateFilter) {
  InSequence s;
  initial_buffer_limit_ = 10;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder_->decodeData(fake_data, false);

    Buffer::OwnedImpl fake_data2("world world");
    decoder_->decodeData(fake_data2, true);
    return Http::okStatus();
  }));

  setUpBufferLimits();
  setupFilterChain(2, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "413"}, {"content-length", "17"}, {"content-type", "text/plain"}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  doRemoteClose(false);
}

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsBeforeHeaders) {
  initial_buffer_limit_ = 10;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Start the response without processing the request headers through all
  // filters.
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early response.

  expectOnDestroy();
  Buffer::OwnedImpl fake_response("A long enough string to go over watermarks");
  // Fake response starts doing through the filter.
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  std::string response_body;
  // The 500 goes directly to the encoder.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        // Make sure this is a 500
        EXPECT_EQ("500", headers.getStatusValue());
        // Make sure Envoy standard sanitization has been applied.
        EXPECT_TRUE(headers.Date() != nullptr);
        EXPECT_EQ("response_payload_too_large",
                  decoder_filters_[0]->callbacks_->streamInfo().responseCodeDetails().value());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);
  EXPECT_EQ("Internal Server Error", response_body);

  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsAfterHeaders) {
  initial_buffer_limit_ = 10;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Start the response, and make sure the request headers are fully processed.
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early reset.
  const std::string data = "A long enough string to go over watermarks";
  Buffer::OwnedImpl fake_response(data);
  InSequence s;
  EXPECT_CALL(stream_, removeCallbacks(_));
  expectOnDestroy(false);
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(stream_, resetStream(_));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_LOG_CONTAINS(
      "debug",
      "Resetting stream due to response_payload_too_large. Prior headers have already been sent",
      decoder_filters_[0]->callbacks_->encodeData(fake_response, false););
  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

TEST_F(HttpConnectionManagerImplTest, FilterHeadReply) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "HEAD"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::Continue;
      }));

  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillOnce(Return(true));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ("11", headers.getContentLengthValue());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol(Envoy::Http::Protocol::Http11));
  conn_manager_->onData(fake_input, false);
}

// Verify that if an encoded stream has been ended, but gets stopped by a filter chain, we end
// up resetting the stream in the doEndStream() path (e.g., via filter reset due to timeout, etc.),
// we emit a reset to the codec.
TEST_F(HttpConnectionManagerImplTest, ResetWithStoppedFilter) {
  setup(false, "");
  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::Continue;
      }));

  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillOnce(Return(true));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ("11", headers.getContentLengthValue());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterDataStatus {
        return FilterDataStatus::StopIterationAndBuffer;
      }));

  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the request
  startRequest(true);

  EXPECT_CALL(response_encoder_.stream_, resetStream(_));
  expectOnDestroy();
  encoder_filters_[0]->callbacks_->resetStream();
}

// Filter continues headers iteration without ending the stream, then injects a body later.
TEST_F(HttpConnectionManagerImplTest, FilterContinueDontEndStreamInjectBody) {
  setup(false, "");
  setupFilterChain(2, 2);

  // Decode filter 0 changes end_stream to false.
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndDontEndStream));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  // Kick off the incoming data.
  startRequest(true);

  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Decode filter 0 injects request body later.
  Buffer::OwnedImpl data("hello");
  decoder_filters_[0]->callbacks_->injectDecodedDataToFilterChain(data, true);

  // Encode filter 1 changes end_stream to false.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndDontEndStream));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      makeHeaderMap<TestResponseHeaderMapImpl>({{":status", "200"}}), true, "details");

  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  // Encode filter 1 injects request body later.
  Buffer::OwnedImpl data2("hello");
  encoder_filters_[1]->callbacks_->injectEncodedDataToFilterChain(data2, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyContinuation) {
  setup(false, "");
  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming request.
  startRequest(true);

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  Buffer::OwnedImpl data("hello");
  decoder_filters_[0]->callbacks_->addDecodedData(data, true);
  decoder_filters_[0]->callbacks_->continueDecoding();

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  Buffer::OwnedImpl data2("hello");
  encoder_filters_[1]->callbacks_->addEncodedData(data2, true);
  encoder_filters_[1]->callbacks_->continueEncoding();
}

// This test verifies proper sequences of decodeData() and encodeData() are called
// when all filers return "CONTINUE" in following case:
//
// 3 decode filters:
//
//   filter0->decodeHeaders(_, true)
//     return CONTINUE
//   filter1->decodeHeaders(_, true)
//     filter1->addDecodeData()
//     return CONTINUE
//   filter2->decodeHeaders(_, false)
//     return CONTINUE
//   filter2->decodeData(_, true)
//     return CONTINUE
//
//   filter0->decodeData(, true) is NOT called.
//   filter1->decodeData(, true) is NOT called.
//
// 3 encode filters:
//
//   filter2->encodeHeaders(_, true)
//     return CONTINUE
//   filter1->encodeHeaders(_, true)
//     filter1->addEncodeData()
//     return CONTINUE
//   filter0->decodeHeaders(_, false)
//     return CONTINUE
//   filter0->decodeData(_, true)
//     return CONTINUE
//
//   filter2->encodeData(, true) is NOT called.
//   filter1->encodeData(, true) is NOT called.
//
TEST_F(HttpConnectionManagerImplTest, AddDataWithAllContinue) {
  setup(false, "");
  setupFilterChain(3, 3);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data2("hello");
        decoder_filters_[1]->callbacks_->addDecodedData(data2, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true)).Times(0);
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true)).Times(0);

  // Kick off the incoming data.
  startRequest(true);

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data2("goodbyte");
        encoder_filters_[1]->callbacks_->addEncodedData(data2, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true)).Times(0);

  decoder_filters_[2]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[2]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
}

// This test verifies proper sequences of decodeData() and encodeData() are called
// when the first filer is "stopped" and "continue" in following case:
//
// 3 decode filters:
//
//   filter0->decodeHeaders(_, true)
//     return STOP
//   filter0->continueDecoding()
//   filter1->decodeHeaders(_, true)
//     filter1->addDecodeData()
//     return CONTINUE
//   filter2->decodeHeaders(_, false)
//     return CONTINUE
//   filter2->decodeData(_, true)
//     return CONTINUE
//
//   filter0->decodeData(, true) is NOT called.
//   filter1->decodeData(, true) is NOT called.
//
// 3 encode filters:
//
//   filter2->encodeHeaders(_, true)
//     return STOP
//   filter2->continueEncoding()
//   filter1->encodeHeaders(_, true)
//     filter1->addEncodeData()
//     return CONTINUE
//   filter0->decodeHeaders(_, false)
//     return CONTINUE
//   filter0->decodeData(_, true)
//     return CONTINUE
//
//   filter2->encodeData(, true) is NOT called.
//   filter1->encodeData(, true) is NOT called.
//
TEST_F(HttpConnectionManagerImplTest, AddDataWithStopAndContinue) {
  setup(false, "");

  setupFilterChain(3, 3);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the request.
  startRequest(true);

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data2("hello");
        decoder_filters_[1]->callbacks_->addDecodedData(data2, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  // This fail, it is called twice.
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true)).Times(0);
  // This fail, it is called once
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true)).Times(0);

  decoder_filters_[0]->callbacks_->continueDecoding();

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());

  decoder_filters_[2]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[2]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data2("goodbyte");
        encoder_filters_[1]->callbacks_->addEncodedData(data2, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true)).Times(0);

  encoder_filters_[2]->callbacks_->continueEncoding();
}

// Use filter direct decode/encodeData() calls without trailers.
TEST_F(HttpConnectionManagerImplTest, FilterDirectDecodeEncodeDataNoTrailers) {
  setup(false, "");
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _));
  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl decode_buffer;
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        decode_buffer.move(data);
        return FilterDataStatus::StopIterationNoBuffer;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the request.
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol(Envoy::Http::Protocol::Http11));
  startRequest(true, "hello");

  Buffer::OwnedImpl decoded_data_to_forward;
  decoded_data_to_forward.move(decode_buffer, 2);
  EXPECT_CALL(*decoder_filters_[1], decodeData(BufferStringEqual("he"), false))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[0]->callbacks_->injectDecodedDataToFilterChain(decoded_data_to_forward, false);

  EXPECT_CALL(*decoder_filters_[1], decodeData(BufferStringEqual("llo"), true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->injectDecodedDataToFilterChain(decode_buffer, true);

  // Response path.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  Buffer::OwnedImpl encoder_buffer;
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        encoder_buffer.move(data);
        return FilterDataStatus::StopIterationNoBuffer;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, true);

  Buffer::OwnedImpl encoded_data_to_forward;
  encoded_data_to_forward.move(encoder_buffer, 3);
  EXPECT_CALL(*encoder_filters_[0], encodeData(BufferStringEqual("res"), false));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  encoder_filters_[1]->callbacks_->injectEncodedDataToFilterChain(encoded_data_to_forward, false);

  EXPECT_CALL(*encoder_filters_[0], encodeData(BufferStringEqual("ponse"), true));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();
  encoder_filters_[1]->callbacks_->injectEncodedDataToFilterChain(encoder_buffer, true);
}

// Use filter direct decode/encodeData() calls with trailers.
TEST_F(HttpConnectionManagerImplTest, FilterDirectDecodeEncodeDataTrailers) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder_->decodeData(fake_data, false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _));
  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl decode_buffer;
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        decode_buffer.move(data);
        return FilterDataStatus::StopIterationNoBuffer;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  Buffer::OwnedImpl decoded_data_to_forward;
  decoded_data_to_forward.move(decode_buffer, 2);
  EXPECT_CALL(*decoder_filters_[1], decodeData(BufferStringEqual("he"), false))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[0]->callbacks_->injectDecodedDataToFilterChain(decoded_data_to_forward, false);

  EXPECT_CALL(*decoder_filters_[1], decodeData(BufferStringEqual("llo"), false))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[0]->callbacks_->injectDecodedDataToFilterChain(decode_buffer, false);

  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Response path.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  Buffer::OwnedImpl encoder_buffer;
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        encoder_buffer.move(data);
        return FilterDataStatus::StopIterationNoBuffer;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());

  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, false);
  decoder_filters_[1]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});

  Buffer::OwnedImpl encoded_data_to_forward;
  encoded_data_to_forward.move(encoder_buffer, 3);
  EXPECT_CALL(*encoder_filters_[0], encodeData(BufferStringEqual("res"), false));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  encoder_filters_[1]->callbacks_->injectEncodedDataToFilterChain(encoded_data_to_forward, false);

  EXPECT_CALL(*encoder_filters_[0], encodeData(BufferStringEqual("ponse"), false));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  encoder_filters_[1]->callbacks_->injectEncodedDataToFilterChain(encoder_buffer, false);

  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();
  encoder_filters_[1]->callbacks_->continueEncoding();
}

TEST_F(HttpConnectionManagerImplTest, MultipleFilters) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder_->decodeData(fake_data, false);

    Buffer::OwnedImpl fake_data2("world");
    decoder_->decodeData(fake_data2, true);
    return Http::okStatus();
  }));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _));
  setupFilterChain(3, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(),
                  decoder_filters_[0]->callbacks_->connection()->ssl().get());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol(Envoy::Http::Protocol::Http11));
  conn_manager_->onData(fake_input, false);

  // Mimic a decoder filter that trapped data and now sends it on, since the data was buffered
  // by the first filter, we expect to get it in 1 decodeData() call.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(),
                  decoder_filters_[1]->callbacks_->connection()->ssl().get());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Now start encoding and mimic trapping in the encoding filter.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[1]->callbacks_->connection()->ssl().get());
  decoder_filters_[2]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[2]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[2]->callbacks_->encodeData(response_body, false);
  decoder_filters_[2]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
  EXPECT_EQ(ssl_connection_.get(), decoder_filters_[2]->callbacks_->connection()->ssl().get());

  // Now finish the encode.
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();
  encoder_filters_[1]->callbacks_->continueEncoding();

  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[0]->callbacks_->connection()->ssl().get());
}

TEST(HttpConnectionManagerTracingStatsTest, verifyTracingStats) {
  Stats::IsolatedStoreImpl stats;
  ConnectionManagerTracingStats tracing_stats{CONN_MAN_TRACING_STATS(POOL_COUNTER(stats))};

  EXPECT_THROW(
      ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::HealthCheck, tracing_stats),
      std::invalid_argument);

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::ClientForced, tracing_stats);
  EXPECT_EQ(1UL, tracing_stats.client_enabled_.value());

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::NotTraceableRequestId, tracing_stats);
  EXPECT_EQ(1UL, tracing_stats.not_traceable_.value());

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::Sampling, tracing_stats);
  EXPECT_EQ(1UL, tracing_stats.random_sampling_.value());
}

TEST_F(HttpConnectionManagerImplTest, NoNewStreamWhenOverloaded) {
  Server::OverloadActionState stop_accepting_requests = Server::OverloadActionState(0.8);
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().StopAcceptingRequests))
      .WillByDefault(ReturnRef(stop_accepting_requests));

  setup(false, "");

  EXPECT_CALL(random_, random())
      .WillRepeatedly(Return(static_cast<float>(Random::RandomGenerator::max()) * 0.5));

  // 503 direct response when overloaded.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("503", headers.getStatusValue());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  startRequest();

  EXPECT_EQ("envoy overloaded", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_overload_close_.value());
}

TEST_F(HttpConnectionManagerImplTest, DisableHttp1KeepAliveWhenOverloaded) {
  Server::OverloadActionState disable_http_keep_alive = Server::OverloadActionState(0.8);
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().DisableHttpKeepAlive))
      .WillByDefault(ReturnRef(disable_http_keep_alive));

  codec_->protocol_ = Protocol::Http11;
  setup(false, "");

  EXPECT_CALL(random_, random())
      .WillRepeatedly(Return(static_cast<float>(Random::RandomGenerator::max()) * 0.5));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                                 {":path", "/"},
                                                                 {":method", "GET"},
                                                                 {"connection", "keep-alive"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  EXPECT_EQ(1U, stats_.named_.downstream_cx_overload_disable_keepalive_.value());
}

class DrainH2HttpConnectionManagerImplTest : public HttpConnectionManagerImplTest,
                                             public testing::WithParamInterface<bool> {
public:
  DrainH2HttpConnectionManagerImplTest() {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2", "true"}});
  }

private:
  TestScopedRuntime runtime_;
};

// Verify that, if the runtime option is enabled, HTTP2 connections will receive
// a GOAWAY message when the overload action is triggered.
TEST_P(DrainH2HttpConnectionManagerImplTest, DisableHttp2KeepAliveWhenOverloaded) {
  Server::OverloadActionState disable_http_keep_alive = Server::OverloadActionState::saturated();
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().DisableHttpKeepAlive))
      .WillByDefault(ReturnRef(disable_http_keep_alive));

  codec_->protocol_ = Protocol::Http2;
  setup(false, "");
  if (GetParam()) {
    EXPECT_CALL(*codec_, shutdownNotice);
  }

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                                 {":path", "/"},
                                                                 {":method", "GET"},
                                                                 {"connection", "keep-alive"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  Mock::VerifyAndClearExpectations(codec_);
  EXPECT_EQ(1, stats_.named_.downstream_cx_overload_disable_keepalive_.value());
}

INSTANTIATE_TEST_SUITE_P(WithRuntimeOverride, DrainH2HttpConnectionManagerImplTest,
                         testing::Bool());

TEST_F(HttpConnectionManagerImplTest, TestStopAllIterationAndBufferOnDecodingPathFirstFilter) {
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(true, true);

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Verify that once the decoder_filters_[0]'s continueDecoding() is called, decoder_filters_[1]'s
  // decodeHeaders() is called, and both filters receive data and trailers consequently.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, _))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, TestStopAllIterationAndBufferOnDecodingPathSecondFilter) {
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(true, false);

  // Verify headers go through both filters, and data and trailers go through the first filter only.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, _))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Verify that once the decoder_filters_[1]'s continueDecoding() is called, both data and trailers
  // go through the second filter.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[1]->callbacks_->continueDecoding();

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, TestStopAllIterationAndBufferOnEncodingPath) {
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // encoder_filters_[1] is the first filter in the chain.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Invoke([&](HeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopAllIterationAndBuffer;
      }));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  // Invoke encodeData while all iteration is stopped and make sure the filters do not have
  // encodeData called.
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, _)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, _)).Times(0);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, false);
  decoder_filters_[0]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});

  // Verify that once encoder_filters_[1]'s continueEncoding() is called, encoder_filters_[0]'s
  // encodeHeaders() is called, and both filters receive data and trailers consequently.
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, _))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, _)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, _));
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  expectOnDestroy();
  encoder_filters_[1]->callbacks_->continueEncoding();
}

TEST_F(HttpConnectionManagerImplTest, DisableKeepAliveWhenDraining) {
  setup(false, "");

  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                                 {":path", "/"},
                                                                 {":method", "GET"},
                                                                 {"connection", "keep-alive"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestSessionTrace) {
  setup(false, "");

  // Set up the codec.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        data.drain(4);
        return Http::okStatus();
      }));
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  setupFilterChain(1, 1);

  // Create a new stream
  decoder_ = &conn_manager_->newStream(response_encoder_);

  // Send headers to that stream, and verify we both set and clear the tracked object.
  {
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_))
        .Times(2)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> const ScopeTrackedObject* {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state,
                      testing::HasSubstr("filter_manager_callbacks_.requestHeaders():   empty"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
          return nullptr;
        }))
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
        .WillOnce(Invoke([](HeaderMap&, bool) -> FilterHeadersStatus {
          return FilterHeadersStatus::StopIteration;
        }));
    decoder_->decodeHeaders(std::move(headers), false);
  }

  // Send trailers to that stream, and verify by this point headers are in logged state.
  {
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_))
        .Times(2)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> const ScopeTrackedObject* {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state, testing::HasSubstr("filter_manager_callbacks_.requestHeaders(): \n"));
          EXPECT_THAT(state, testing::HasSubstr("':authority', 'host'\n"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
          return nullptr;
        }))
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(*decoder_filters_[0], decodeComplete());
    EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
    decoder_->decodeTrailers(std::move(trailers));
  }

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// SRDS no scope found.
TEST_F(HttpConnectionManagerImplTest, TestSrdsRouteNotFound) {
  setup(false, "", true, true);
  setupFilterChain(1, 0); // Recreate the chain for second stream.

  EXPECT_CALL(*static_cast<const Router::MockScopedConfig*>(
                  scopedRouteConfigProvider()->config<Router::ScopedConfig>().get()),
              getRouteConfig(_))
      .Times(2)
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[0]->callbacks_->route());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete()); // end_stream=true.

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// SRDS updating scopes affects routing.
TEST_F(HttpConnectionManagerImplTest, TestSrdsUpdate) {
  setup(false, "", true, true);

  EXPECT_CALL(*static_cast<const Router::MockScopedConfig*>(
                  scopedRouteConfigProvider()->config<Router::ScopedConfig>().get()),
              getRouteConfig(_))
      .Times(3)
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))        // refreshCachedRoute first time.
      .WillOnce(Return(route_config_)); // triggered by callbacks_->route(), SRDS now updated.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));
  const std::string fake_cluster1_name = "fake_cluster1";
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));
  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(fake_cluster1.get()));
  EXPECT_CALL(*route_config_, route(_, _, _, _)).WillOnce(Return(route1));
  // First no-scope-found request will be handled by decoder_filters_[0].
  setupFilterChain(1, 0);
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[0]->callbacks_->route());

        // Clear route and next call on callbacks_->route() will trigger a re-snapping of the
        // snapped_route_config_.
        decoder_filters_[0]->callbacks_->clearRouteCache();

        // Now route config provider returns something.
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1->routeEntry(), decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());
        return FilterHeadersStatus::StopIteration;

        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete()); // end_stream=true.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// SRDS Scope header update cause cross-scope reroute.
TEST_F(HttpConnectionManagerImplTest, TestSrdsCrossScopeReroute) {
  setup(false, "", true, true);

  std::shared_ptr<Router::MockConfig> route_config1 =
      std::make_shared<NiceMock<Router::MockConfig>>();
  std::shared_ptr<Router::MockConfig> route_config2 =
      std::make_shared<NiceMock<Router::MockConfig>>();
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  std::shared_ptr<Router::MockRoute> route2 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*route_config1, route(_, _, _, _)).WillRepeatedly(Return(route1));
  EXPECT_CALL(*route_config2, route(_, _, _, _)).WillRepeatedly(Return(route2));
  EXPECT_CALL(*static_cast<const Router::MockScopedConfig*>(
                  scopedRouteConfigProvider()->config<Router::ScopedConfig>().get()),
              getRouteConfig(_))
      // 1. Snap scoped route config;
      // 2. refreshCachedRoute (both in decodeHeaders(headers,end_stream);
      // 3. then refreshCachedRoute triggered by decoder_filters_[1]->callbacks_->route().
      .Times(3)
      .WillRepeatedly(Invoke([&](const HeaderMap& headers) -> Router::ConfigConstSharedPtr {
        auto& test_headers = dynamic_cast<const TestRequestHeaderMapImpl&>(headers);
        if (test_headers.get_("scope_key") == "foo") {
          return route_config1;
        }
        return route_config2;
      }));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "GET"}, {"scope_key", "foo"}, {":path", "/foo"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    data.drain(4);
    return Http::okStatus();
  }));
  setupFilterChain(2, 0);
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        auto& test_headers = dynamic_cast<TestRequestHeaderMapImpl&>(headers);
        // Clear cached route and change scope key to "bar".
        decoder_filters_[0]->callbacks_->clearRouteCache();
        test_headers.remove("scope_key");
        test_headers.addCopy("scope_key", "bar");
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) -> FilterHeadersStatus {
        auto& test_headers = dynamic_cast<TestRequestHeaderMapImpl&>(headers);
        EXPECT_EQ(test_headers.get_("scope_key"), "bar");
        // Route now switched to route2 as header "scope_key" has changed.
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(route2->routeEntry(), decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// SRDS scoped RouteConfiguration found and route found.
TEST_F(HttpConnectionManagerImplTest, TestSrdsRouteFound) {
  setup(false, "", true, true);
  setupFilterChain(1, 0);

  const std::string fake_cluster1_name = "fake_cluster1";
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));
  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(fake_cluster1.get()));
  EXPECT_CALL(*scopedRouteConfigProvider()->config<Router::MockScopedConfig>(), getRouteConfig(_))
      // 1. decodeHeaders() snapping route config.
      // 2. refreshCachedRoute() later in the same decodeHeaders().
      .Times(2);
  EXPECT_CALL(
      *static_cast<const Router::MockConfig*>(
          scopedRouteConfigProvider()->config<Router::MockScopedConfig>()->route_config_.get()),
      route(_, _, _, _))
      .WillOnce(Return(route1));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1->routeEntry(), decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, NewConnection) {
  setup(false, "", true, true);

  filter_callbacks_.connection_.stream_info_.protocol_ = absl::nullopt;
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol());
  EXPECT_EQ(Network::FilterStatus::Continue, conn_manager_->onNewConnection());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http3_total_.value());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http3_active_.value());

  filter_callbacks_.connection_.stream_info_.protocol_ = Envoy::Http::Protocol::Http3;
  codec_->protocol_ = Http::Protocol::Http3;
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol());
  EXPECT_CALL(*codec_, protocol()).Times(AtLeast(1));
  EXPECT_EQ(Network::FilterStatus::StopIteration, conn_manager_->onNewConnection());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http3_total_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http3_active_.value());
}

TEST_F(HttpConnectionManagerImplTest, TestUpstreamRequestHeadersSize) {
  // Test with Headers only request, No Data, No response.
  setup(false, "");
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  filter_callbacks_.upstreamHost(host_);

  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_headers_size"), 30));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_body_size"), 0));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_body_size"), 0));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, TestUpstreamRequestBodySize) {
  // Test Request with Headers and Data, No response.
  setup(false, "");
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("12345");
    decoder_->decodeData(fake_data, true);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  filter_callbacks_.upstreamHost(host_);

  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_headers_size"), 30));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_body_size"), 5));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_body_size"), 0));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, TestUpstreamResponseHeadersSize) {
  // Test with Header only response.
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("1234");
    decoder_->decodeData(fake_data, true);

    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  filter_callbacks_.upstreamHost(host_);

  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_headers_size"), 30));

  // Response headers are internally mutated and we record final response headers.
  // for example in the below test case, response headers are modified as
  // {':status', '200' 'date', 'Mon, 06 Jul 2020 06:08:55 GMT' 'server', ''}
  // whose size is 49 instead of original response headers size 10({":status", "200"}).
  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_headers_size"), 49));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_body_size"), 4));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_body_size"), 0));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();

  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
}

TEST_F(HttpConnectionManagerImplTest, TestUpstreamResponseBodySize) {
  // Test with response headers and body.
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("1234");
    decoder_->decodeData(fake_data, true);

    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  filter_callbacks_.upstreamHost(host_);

  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_headers_size"), 30));
  EXPECT_CALL(
      host_->cluster_.request_response_size_stats_store_,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_headers_size"), 49));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_body_size"), 4));
  EXPECT_CALL(host_->cluster_.request_response_size_stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rs_body_size"), 11));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");

  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  Buffer::OwnedImpl fake_response("hello-world");
  decoder_filters_[0]->callbacks_->encodeData(fake_response, true);
}

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponseUsingHttp3) {
  setup(false, "envoy-custom-server", false);

  filter_callbacks_.connection_.stream_info_.protocol_ = Envoy::Http::Protocol::Http3;
  codec_->protocol_ = Http::Protocol::Http3;
  EXPECT_EQ(Network::FilterStatus::StopIteration, conn_manager_->onNewConnection());

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Pretend to get a new stream and then fire a headers only request into it. Then we respond into
  // the filter.
  RequestDecoder& decoder = conn_manager_->newStream(response_encoder_);
  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  decoder.decodeHeaders(std::move(headers), true);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http3_total_.value());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http3_active_.value());
}

namespace {

class SimpleType : public StreamInfo::FilterState::Object {
public:
  SimpleType(int value) : value_(value) {}
  int access() const { return value_; }

private:
  int value_;
};

} // namespace

TEST_F(HttpConnectionManagerImplTest, ConnectionFilterState) {
  filter_callbacks_.connection_.stream_info_.filter_state_->setData(
      "connection_provided_data", std::make_shared<SimpleType>(555),
      StreamInfo::FilterState::StateType::ReadOnly);

  setup(false, "envoy-custom-server", false);
  setupFilterChain(1, 0, /* num_requests = */ 3);

  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder_->decodeHeaders(std::move(headers), true);
        return Http::okStatus();
      }));
  {
    InSequence s;
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(Invoke([this](HeaderMap&, bool) -> FilterHeadersStatus {
          decoder_filters_[0]->callbacks_->streamInfo().filterState()->setData(
              "per_filter_chain", std::make_unique<SimpleType>(1),
              StreamInfo::FilterState::StateType::ReadOnly,
              StreamInfo::FilterState::LifeSpan::FilterChain);
          decoder_filters_[0]->callbacks_->streamInfo().filterState()->setData(
              "per_downstream_request", std::make_unique<SimpleType>(2),
              StreamInfo::FilterState::StateType::ReadOnly,
              StreamInfo::FilterState::LifeSpan::Request);
          decoder_filters_[0]->callbacks_->streamInfo().filterState()->setData(
              "per_downstream_connection", std::make_unique<SimpleType>(3),
              StreamInfo::FilterState::StateType::ReadOnly,
              StreamInfo::FilterState::LifeSpan::Connection);
          return FilterHeadersStatus::StopIteration;
        }));
    EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
        .WillOnce(Invoke([this](HeaderMap&, bool) -> FilterHeadersStatus {
          EXPECT_FALSE(
              decoder_filters_[1]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_filter_chain"));
          EXPECT_TRUE(
              decoder_filters_[1]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_downstream_request"));
          EXPECT_TRUE(
              decoder_filters_[1]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_downstream_connection"));
          EXPECT_TRUE(
              decoder_filters_[1]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "connection_provided_data"));
          return FilterHeadersStatus::StopIteration;
        }));
    EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, true))
        .WillOnce(Invoke([this](HeaderMap&, bool) -> FilterHeadersStatus {
          EXPECT_FALSE(
              decoder_filters_[2]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_filter_chain"));
          EXPECT_FALSE(
              decoder_filters_[2]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_downstream_request"));
          EXPECT_TRUE(
              decoder_filters_[2]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "per_downstream_connection"));
          EXPECT_TRUE(
              decoder_filters_[1]->callbacks_->streamInfo().filterState()->hasData<SimpleType>(
                  "connection_provided_data"));
          return FilterHeadersStatus::StopIteration;
        }));
  }

  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
  decoder_filters_[0]->callbacks_->recreateStream();
  conn_manager_->onData(fake_input, false);

  // The connection life time data should have been written to the connection filter state.
  EXPECT_TRUE(filter_callbacks_.connection_.stream_info_.filter_state_->hasData<SimpleType>(
      "per_downstream_connection"));
  EXPECT_CALL(*decoder_filters_[1], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[1], onDestroy());
  EXPECT_CALL(*decoder_filters_[2], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[2], onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

class HttpConnectionManagerImplDeathTest : public HttpConnectionManagerImplTest {
public:
  Router::RouteConfigProvider* routeConfigProvider() override {
    return route_config_provider2_.get();
  }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    return scoped_route_config_provider2_.get();
  }

  std::shared_ptr<Router::MockRouteConfigProvider> route_config_provider2_;
  std::shared_ptr<Router::MockScopedRouteConfigProvider> scoped_route_config_provider2_;
};

// HCM config can only have either RouteConfigProvider or ScopedRoutesConfigProvider.
TEST_F(HttpConnectionManagerImplDeathTest, InvalidConnectionManagerConfig) {
  setup(false, "");

  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));
  // Either RDS or SRDS should be set.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or scopedRouteConfigProvider should be set in "
                     "ConnectionManagerImpl.");

  route_config_provider2_ = std::make_shared<NiceMock<Router::MockRouteConfigProvider>>();

  // Only route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));

  scoped_route_config_provider2_ =
      std::make_shared<NiceMock<Router::MockScopedRouteConfigProvider>>();
  // Can't have RDS and SRDS provider in the same time.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or scopedRouteConfigProvider should be set in "
                     "ConnectionManagerImpl.");

  route_config_provider2_.reset();
  // Only scoped route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

} // namespace Http
} // namespace Envoy
