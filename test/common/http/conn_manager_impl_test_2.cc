#include <chrono>

#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/common/http/custom_header_extension.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Ref;
using testing::Return;
using testing::ReturnArg;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  setup();
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
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestCompleteWithUpstreamHalfCloseEnabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true"}});
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest();

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
      }));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  // Half closing upstream connection does not cause the stream to be reset
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _))
      .Times(2);

  // Half closing both downstream and upstream triggers full stream close
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Buffer::OwnedImpl fake_data("the end");
    decoder_->decodeData(fake_data, true);
    return Http::okStatus();
  }));
  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete10) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http10));
  setup();
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
  // When framing by connection close, by default Envoy will FlushWrite, no delay.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite, _));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete10NoOptimize) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  EXPECT_CALL(runtime_.snapshot_, getBoolean(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http10));
  setup();
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
  // When framing by connection close, by default Envoy will FlushWrite, no delay but with a runtime
  // override, it will still flush close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, DisconnectOnProxyConnectionDisconnect) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  setup();

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
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, ResponseStartBeforeRequestComplete) {
  setup(SetupOpts().setServerName(""));

  // This is like ResponseBeforeRequestComplete, but it tests the case where we start the reply
  // before the request completes, but don't finish the reply until after the request completes.
  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb factory =
            createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(Invoke([&](Buffer::Instance&, bool) {
    response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
  }));
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));
  Buffer::OwnedImpl fake_response("world");
  filter->callbacks_->encodeData(fake_response, true);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamDisconnect) {
  InSequence s;
  setup();

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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return codecProtocolError("protocol error");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_)).Times(2);
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // A protocol exception should result in reset of the streams followed by a remote or local close
  // depending on whether the downstream client closes the connection prior to the delayed close
  // timer firing.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamProtocolErrorAccessLog) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};
  setup();

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(
                stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::DownstreamProtocolError));
          }));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return codecProtocolError("protocol error");
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamProtocolErrorAfterHeadersAccessLog) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(
                stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::DownstreamProtocolError));
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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return bufferFloodError("too many outbound frames");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_)).Times(2);
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // FrameFloodException should result in reset of the streams followed by abortive close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  EXPECT_CALL(*log_handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
            EXPECT_EQ("codec_error:too_many_outbound_frames",
                      stream_info.responseCodeDetails().value());
          }));
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_LOG_NOT_CONTAINS("warning", "downstream HTTP flood",
                          conn_manager_->onData(fake_input, false));

  EXPECT_TRUE(filter_callbacks_.connection_.streamInfo().hasResponseFlag(
      StreamInfo::CoreResponseFlag::DownstreamProtocolError));
}

TEST_F(HttpConnectionManagerImplTest, EnvoyOverloadError) {
  std::shared_ptr<AccessLog::MockInstance> log_handler =
      std::make_shared<NiceMock<AccessLog::MockInstance>>();
  access_logs_ = {log_handler};
  setup();
  ASSERT_EQ(0U, stats_.named_.downstream_rq_overload_close_.value());

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return envoyOverloadError("Envoy Overloaded");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_)).Times(2);
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // Overload should result in local reply followed by abortive close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));

  EXPECT_CALL(*log_handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
            EXPECT_EQ("overload_error:Envoy_Overloaded", stream_info.responseCodeDetails().value());
          }));
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_TRUE(filter_callbacks_.connection_.streamInfo().hasResponseFlag(
      StreamInfo::CoreResponseFlag::OverloadManager));
  EXPECT_EQ(1U, stats_.named_.downstream_rq_overload_close_.value());
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeoutNoCodec) {
  // Not used in the test.
  delete codec_;

  idle_timeout_ = (std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite, _));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeout) {
  idle_timeout_ = (std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup();

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  idle_timer->invokeCallback();

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));
  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDurationResponseFlag) {
  // Not used in the test.
  delete codec_;

  max_connection_duration_ = (std::chrono::milliseconds(10));
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite, _));
  filter_callbacks_.connection_.streamInfo().setResponseFlag(
      StreamInfo::CoreResponseFlag::DurationTimeout);
  EXPECT_CALL(*connection_duration_timer, disableTimer());

  connection_duration_timer->invokeCallback();

  EXPECT_TRUE(filter_callbacks_.connection_.streamInfo().hasResponseFlag(
      StreamInfo::CoreResponseFlag::DurationTimeout));

  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDurationNoCodec) {
  // Not used in the test.
  delete codec_;

  max_connection_duration_ = (std::chrono::milliseconds(10));
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite, _));
  EXPECT_CALL(*connection_duration_timer, disableTimer());

  connection_duration_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/19045
TEST_F(HttpConnectionManagerImplTest, MaxRequests) {
  max_requests_per_connection_ = 1;
  codec_->protocol_ = Protocol::Http2;
  setup();

  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(*codec_, shutdownNotice());
  EXPECT_CALL(*drain_timer, disableTimer());

  // Kick off two requests.
  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
  conn_manager_->onData(fake_input, false);
  drain_timer->invokeCallback();

  EXPECT_EQ(2U, stats_.named_.downstream_cx_max_requests_reached_.value());

  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// max_requests_per_connection is met first then the drain timer fires. Drain timer should be
// ignored.
TEST_F(HttpConnectionManagerImplTest, DrainConnectionUponCompletionVsOnDrainTimeoutHttp11) {
  // Http1.1 is used for this test because it defaults to keeping the connection alive.
  EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http11));
  max_requests_per_connection_ = 2;
  max_connection_duration_ = std::chrono::milliseconds(10);

  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  // Set up connection.
  setup();

  // Create a filter so we can encode responses.
  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  startRequest(true);
  // Encode response, connection will not be closed since we're using http 1.1.
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

  // Now connection is established and codec is not nullptr. This should start the drain timer.
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  connection_duration_timer->invokeCallback();
  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());

  // Get a fresh mock filter.
  filter = new NiceMock<MockStreamDecoderFilter>();
  // Send a second request. This will cause max_requests_per_connection limit to be reached.
  // Connection drain state will be set to closing.
  startRequest(true);
  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_requests_reached_.value());

  drain_timer->invokeCallback();

  // Send the last response. The drain timer having already fired should not be an issue.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDuration) {
  max_connection_duration_ = (std::chrono::milliseconds(10));
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup();

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  connection_duration_timer->invokeCallback();

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));
  EXPECT_CALL(*connection_duration_timer, disableTimer());
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());
}

TEST_F(HttpConnectionManagerImplTest, ConnectionDurationSafeHttp1) {
  EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http10));
  max_connection_duration_ = std::chrono::milliseconds(10);
  Event::MockTimer* connection_duration_timer = setUpTimer();
  EXPECT_CALL(*connection_duration_timer, enableTimer(_, _));
  setup(SetupOpts().setHttp1SafeMaxConnectionDuration(true));

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  startRequest(true, "hello");

  EXPECT_CALL(*connection_duration_timer, disableTimer());
  connection_duration_timer->invokeCallback();
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http1_soft_drain_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_max_duration_reached_.value());

  // Connection manager now waits to send another response, adds the Connection:close header to it,
  // then closes the connection.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, _))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) {
        // Check that the connection:close header is present.
        ASSERT_NE(headers.Connection(), nullptr);
        EXPECT_EQ(headers.getConnectionValue(), Headers::get().ConnectionValues.Close);
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
      }));
  // Expect stream & connection to close after response is sent.
  expectOnDestroy();

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, IntermediateBufferingEarlyResponse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  setup();

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
  setup();
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
  setup();
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
  setup();

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
  setup();
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
  setup();

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
  setup();

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
  setup();

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
  setup();

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
  setup();
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

TEST_F(HttpConnectionManagerImplTest, BlockRouteCacheTest) {
  setup();

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  auto mock_route_0 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(mock_route_0));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  filter->callbacks_->downstreamCallbacks()->clearRouteCache();
  auto mock_route_1 = std::make_shared<NiceMock<Router::MockRoute>>();

  // Refresh cached route after cache is cleared.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(mock_route_1));
  EXPECT_EQ(filter->callbacks_->route().get(), mock_route_1.get());

  auto mock_route_2 = std::make_shared<NiceMock<Router::MockRoute>>();

  // We can also set route directly.
  filter->callbacks_->downstreamCallbacks()->setRoute(mock_route_2);
  EXPECT_EQ(filter->callbacks_->route().get(), mock_route_2.get());

  ResponseHeaderMapPtr response_headers{
      new TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "2"}}};

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  EXPECT_ENVOY_BUG(
      {
        // The cached route will not be cleared after response headers are sent.
        filter->callbacks_->downstreamCallbacks()->clearRouteCache();
        EXPECT_EQ(filter->callbacks_->route().get(), mock_route_2.get());

        // We cannot set route after response headers are sent.
        filter->callbacks_->downstreamCallbacks()->setRoute(nullptr);
        EXPECT_EQ(filter->callbacks_->route().get(), mock_route_2.get());
      },
      "Should never try to refresh or clear the route cache when it is blocked!");

  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  Buffer::OwnedImpl response_data("ok");
  filter->callbacks_->encodeData(response_data, true);
}

TEST_F(HttpConnectionManagerImplTest, Filter) {
  setup();

  setupFilterChain(3, 2);
  const std::string fake_cluster1_name = "fake_cluster1";
  const std::string fake_cluster2_name = "fake_cluster2";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_))
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
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->streamInfo().route());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->streamInfo().route());
        // RDS & CDS consistency problem: route2 points to fake_cluster2, which doesn't exist.
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->clusterInfo());
        decoder_filters_[1]->callbacks_->downstreamCallbacks()->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->clusterInfo());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->streamInfo().route());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  // Kick off the incoming data.
  startRequest(true);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that a filter doing setRoute(nullptr) doesn't cause unexpected problems for filters down
// the line. Also tests that setRoute(nullptr) is equivalent to attempting route resolution and
// failing to find a route.
TEST_F(HttpConnectionManagerImplTest, FilterSetRouteToNullPtr) {
  setup();

  setupFilterChain(2, 1);
  const std::string fake_cluster1_name = "fake_cluster1";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{fake_cluster1_name}))
      .WillRepeatedly(Return(fake_cluster1.get()));

  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));

  // Only called once because setRoute(nullptr) means route resolution won't be attempted again
  // (cached_route_.has_value() becomes true).
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .Times(1)
      .WillOnce(Return(route1));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->streamInfo().route());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->setRoute(nullptr);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->streamInfo().route());
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->clusterInfo());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  startRequest(true);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, UpstreamWatermarkCallbacks) {
  setup();
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Mimic the upstream connection backing up. The router would call
  // onDecoderFilterAboveWriteBufferHighWatermark which should readDisable the stream and increment
  // stats.
  auto& stream = response_encoder_.stream_;
  EXPECT_CALL(stream, readDisable(true));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_paused_reading_total_.value());

  // Resume the flow of data. When the router buffer drains it calls
  // onDecoderFilterBelowWriteBufferLowWatermark which should re-enable reads on the stream.
  EXPECT_CALL(stream, readDisable(false));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_resumed_reading_total_.value());

  // Backup upstream once again.
  EXPECT_CALL(stream, readDisable(true));
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
  setup();

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
  setup();

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
  setup();
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
  setup();
  setUpEncoderAndDecoder(false, false);

  // The filter is a streaming filter. Sending 4 bytes should hit the
  // watermark limit and disable reads on the stream.
  EXPECT_CALL(response_encoder_.stream_, readDisable(true));
  sendRequestHeadersAndData();

  // Change the limit so the buffered data is below the new watermark. The
  // stream should be read-enabled
  EXPECT_CALL(response_encoder_.stream_, readDisable(false));
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

  EXPECT_CALL(*log_handler_, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(stream_info.hasResponseFlag(
                StreamInfo::CoreResponseFlag::DownstreamConnectionTermination));
          }));

  expectOnDestroy();
  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_)).Times(2);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimits) {
  initial_buffer_limit_ = 10;
  streaming_filter_ = false;
  setup();
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

TEST_F(HttpConnectionManagerImplTest, DownstreamConnectionTermination) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};

  setup();
  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(stream_info.hasResponseFlag(
                StreamInfo::CoreResponseFlag::DownstreamConnectionTermination));
          }));

  // Start the request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

// Return 413 from an intermediate filter and make sure we don't continue the filter chain.
TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimitsIntermediateFilter) {
  {
    InSequence s;
    initial_buffer_limit_ = 10;
    setup();

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
  }

  doRemoteClose(false);
}

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsBeforeHeaders) {
  initial_buffer_limit_ = 10;
  setup();
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
  setup();
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
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(response_encoder_.stream_, resetStream(_));
  EXPECT_LOG_CONTAINS(
      "debug",
      "Resetting stream due to response_payload_too_large. Prior headers have already been sent",
      decoder_filters_[0]->callbacks_->encodeData(fake_response, false););
  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());

  // Clean up connection
  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  expectOnDestroy(false);
  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest, FilterHeadReply) {
  InSequence s;
  setup();

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
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillOnce(Return(true));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ("11", headers.getContentLengthValue());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap&, bool) {
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
      }));
  expectOnDestroy();
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, protocol(Envoy::Http::Protocol::Http11));
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, LocalReplyStopsDecoding) {
  InSequence s;
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    // Start POST request (end_stream = false)
    decoder_->decodeHeaders(std::move(headers), false);
    data.drain(4);
    return Http::okStatus();
  }));

  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillOnce(Return(true));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));

  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(Invoke([&](Buffer::Instance&, bool) {
    response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
  }));
  expectOnDestroy();
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Verify that if an encoded stream has been ended, but gets stopped by a filter chain, we end
// up resetting the stream in the doEndStream() path (e.g., via filter reset due to timeout, etc.),
// we emit a reset to the codec.
TEST_F(HttpConnectionManagerImplTest, ResetWithStoppedFilter) {
  setup();
  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::StopIteration;
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

// Verify that the response flag is DownstreamRemoteReset when got
// StreamResetReason::ConnectError from codec.
TEST_F(HttpConnectionManagerImplTest, DownstreamRemoteResetConnectError) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};

  setup();
  codec_->protocol_ = Protocol::Http2;
  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(
                stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::DownstreamRemoteReset));
          }));

  // Start the request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
  response_encoder_.stream_.setDetails("http2.remote_reset");
  response_encoder_.stream_.resetStream(StreamResetReason::ConnectError);
}

// Verify that the response flag is DownstreamRemoteReset when got
// StreamResetReason::RemoteReset from codec.
TEST_F(HttpConnectionManagerImplTest, DownstreamRemoteReset) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};

  setup();
  codec_->protocol_ = Protocol::Http2;
  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(
                stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::DownstreamRemoteReset));
          }));

  // Start the request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
  response_encoder_.stream_.setDetails("http2.remote_reset");
  response_encoder_.stream_.resetStream(StreamResetReason::RemoteReset);
}

// Verify that the response flag is DownstreamRemoteReset when got
// StreamResetReason::RemoteRefusedStreamReset from codec.
TEST_F(HttpConnectionManagerImplTest, DownstreamRemoteResetRefused) {
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());
  access_logs_ = {handler};

  setup();
  codec_->protocol_ = Protocol::Http2;
  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(
                stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::DownstreamRemoteReset));
          }));

  // Start the request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
  response_encoder_.stream_.setDetails("http2.remote_refuse");
  response_encoder_.stream_.resetStream(StreamResetReason::RemoteRefusedStreamReset);
}

// Filter stops headers iteration without ending the stream, then injects a body later.
TEST_F(HttpConnectionManagerImplTest, FilterStopIterationInjectBody) {
  setup();
  setupFilterChain(2, 2);

  // Decode filter 0 changes end_stream to false.
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
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
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
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

// Filter continues headers iteration without ending the stream, then injects a body later.
TEST_F(HttpConnectionManagerImplTest, FilterContinueDontEndStreamInjectBody) {
  setup();
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
  setup();
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
  setup();
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
// when the first filter is "stopped" and "continue" in following case:
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
  setup();

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
  setup();
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
  setup();

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
  setup();

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

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::ClientForced, tracing_stats);
  EXPECT_EQ(1UL, tracing_stats.client_enabled_.value());

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::HealthCheck, tracing_stats);
  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::NotTraceable, tracing_stats);
  EXPECT_EQ(2UL, tracing_stats.not_traceable_.value());

  ConnectionManagerImpl::chargeTracingStats(Tracing::Reason::Sampling, tracing_stats);
  EXPECT_EQ(1UL, tracing_stats.random_sampling_.value());
}

TEST_F(HttpConnectionManagerImplTest, NoNewStreamWhenOverloaded) {
  Server::OverloadActionState stop_accepting_requests(UnitFloat(0.8));
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().StopAcceptingRequests))
      .WillByDefault(ReturnRef(stop_accepting_requests));

  setup();

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
  Server::OverloadActionState disable_http_keep_alive(UnitFloat(0.8));
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().DisableHttpKeepAlive))
      .WillByDefault(ReturnRef(disable_http_keep_alive));

  codec_->protocol_ = Protocol::Http11;
  setup();

  EXPECT_CALL(random_, random())
      .WillRepeatedly(Return(static_cast<float>(Random::RandomGenerator::max()) * 0.5));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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

// Verify that HTTP2 connections will receive a GOAWAY message when the overload action is
// triggered.
TEST_F(HttpConnectionManagerImplTest, DisableHttp2KeepAliveWhenOverloaded) {
  Server::OverloadActionState disable_http_keep_alive = Server::OverloadActionState::saturated();
  ON_CALL(overload_manager_.overload_state_,
          getState(Server::OverloadActionNames::get().DisableHttpKeepAlive))
      .WillByDefault(ReturnRef(disable_http_keep_alive));

  codec_->protocol_ = Protocol::Http2;
  setup();
  EXPECT_CALL(*codec_, shutdownNotice);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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

TEST_F(HttpConnectionManagerImplTest, CodecCreationLoadShedPointCanCloseConnection) {
  Server::MockLoadShedPoint close_connection_creating_codec_point;
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmCodecCreation))
      .WillOnce(Return(&close_connection_creating_codec_point));
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmDecodeHeaders))
      .WillOnce(Return(nullptr));

  setup();

  EXPECT_CALL(close_connection_creating_codec_point, shouldShedLoad()).WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);

  delete codec_;
  EXPECT_EQ(1U, stats_.named_.downstream_rq_overload_close_.value());
  EXPECT_TRUE(filter_callbacks_.connection().streamInfo().hasResponseFlag(
      StreamInfo::CoreResponseFlag::OverloadManager));
}

TEST_F(HttpConnectionManagerImplTest, CodecCreationLoadShedPointBypasscheck) {
  Server::MockLoadShedPoint close_connection_creating_codec_point;
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmCodecCreation))
      .WillOnce(Return(&close_connection_creating_codec_point));
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmDecodeHeaders))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HttpDownstreamFilterCheck))
      .WillOnce(Return(nullptr));

  setup();

  EXPECT_CALL(close_connection_creating_codec_point, shouldShedLoad()).WillOnce(Return(false));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    data.drain(2);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("12");
  conn_manager_->onData(fake_input, false);
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(0U, stats_.named_.downstream_rq_overload_close_.value());
  EXPECT_FALSE(filter_callbacks_.connection().streamInfo().hasResponseFlag(
      StreamInfo::CoreResponseFlag::OverloadManager));
}

TEST_F(HttpConnectionManagerImplTest, DecodeHeaderLoadShedPointCanRejectNewStreams) {
  Server::MockLoadShedPoint accept_new_stream_point;
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmDecodeHeaders))
      .WillOnce(Return(&accept_new_stream_point));
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HcmCodecCreation))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(overload_manager_,
              getLoadShedPoint(Server::LoadShedPointName::get().HttpDownstreamFilterCheck))
      .WillRepeatedly(Return(nullptr));

  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(accept_new_stream_point, shouldShedLoad()).WillOnce(Return(true));

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

  // Let the load shed point allow a new stream.
  EXPECT_CALL(accept_new_stream_point, shouldShedLoad()).WillOnce(Return(false));
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  startRequest(true);

  // Clean up.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();

  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true, "details");
}

TEST_F(HttpConnectionManagerImplTest, TestStopAllIterationAndBufferOnDecodingPathFirstFilter) {
  setup(SetupOpts().setTracing(false));
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
  setup(SetupOpts().setTracing(false));
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
  setup(SetupOpts().setTracing(false));
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
  setup();

  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

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

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, pushTrackedObject(_))
        .Times(1)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> void {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state,
                      testing::HasSubstr("filter_manager_callbacks_.requestHeaders():   null"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
        }));
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, popTrackedObject(_));
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
        .WillOnce(Invoke([](HeaderMap&, bool) -> FilterHeadersStatus {
          return FilterHeadersStatus::StopIteration;
        }));
    decoder_->decodeHeaders(std::move(headers), false);
  }

  // Send trailers to that stream, and verify by this point headers are in logged state.
  {
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, pushTrackedObject(_))
        .Times(1)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> void {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state, testing::HasSubstr("filter_manager_callbacks_.requestHeaders(): \n"));
          EXPECT_THAT(state, testing::HasSubstr("':authority', 'host'\n"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
        }));
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, popTrackedObject(_));
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
  setup(SetupOpts().setUseSrds(true));
  setupFilterChain(1, 0); // Recreate the chain for second stream.

  EXPECT_CALL(*static_cast<const Router::MockScopeKeyBuilder*>(scopeKeyBuilder().ptr()),
              computeScopeKey(_))
      .Times(2);
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
  setup(SetupOpts().setUseSrds(true));

  EXPECT_CALL(*static_cast<const Router::MockScopeKeyBuilder*>(scopeKeyBuilder().ptr()),
              computeScopeKey(_))
      .Times(3);
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
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(fake_cluster1.get()));
  EXPECT_CALL(*route_config_, route(_, _, _, _)).WillOnce(Return(route1));
  // First no-scope-found request will be handled by decoder_filters_[0].
  setupFilterChain(1, 0);
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[0]->callbacks_->route());

        // Clear route and next call on callbacks_->route() will trigger a re-snapping of the
        // snapped_route_config_.
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();

        // Now route config provider returns something.
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->streamInfo().route());
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
  setup(SetupOpts().setUseSrds(true));

  std::shared_ptr<Router::MockConfig> route_config1 =
      std::make_shared<NiceMock<Router::MockConfig>>();
  std::shared_ptr<Router::MockConfig> route_config2 =
      std::make_shared<NiceMock<Router::MockConfig>>();
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  std::shared_ptr<Router::MockRoute> route2 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*route_config1, route(_, _, _, _)).WillRepeatedly(Return(route1));
  EXPECT_CALL(*route_config2, route(_, _, _, _)).WillRepeatedly(Return(route2));
  EXPECT_CALL(*static_cast<const Router::MockScopeKeyBuilder*>(scopeKeyBuilder().ptr()),
              computeScopeKey(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](const HeaderMap& headers) -> Router::ScopeKeyPtr {
        auto& test_headers = dynamic_cast<const TestRequestHeaderMapImpl&>(headers);
        if (test_headers.get_("scope_key") == "foo") {
          Router::ScopeKey key;
          return std::make_unique<Router::ScopeKey>(std::move(key));
        }
        return nullptr;
      }));
  EXPECT_CALL(*static_cast<const Router::MockScopedConfig*>(
                  scopedRouteConfigProvider()->config<Router::ScopedConfig>().get()),
              getRouteConfig(_))
      // 1. Snap scoped route config;
      // 2. refreshCachedRoute (both in decodeHeaders(headers,end_stream);
      // 3. then refreshCachedRoute triggered by decoder_filters_[1]->callbacks_->route().
      .Times(3)
      .WillRepeatedly(
          Invoke([&](const Router::ScopeKeyPtr& scope_key) -> Router::ConfigConstSharedPtr {
            if (scope_key != nullptr) {
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
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->streamInfo().route());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// SRDS scoped RouteConfiguration found and route found.
TEST_F(HttpConnectionManagerImplTest, TestSrdsRouteFound) {
  setup(SetupOpts().setUseSrds(true));
  setupFilterChain(1, 0);

  const std::string fake_cluster1_name = "fake_cluster1";
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));
  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(fake_cluster1.get()));
  EXPECT_CALL(*static_cast<const Router::MockScopeKeyBuilder*>(scopeKeyBuilder().ptr()),
              computeScopeKey(_))
      .Times(2);
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
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->streamInfo().route());
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
  setup(SetupOpts().setUseSrds(true));

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

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponseUsingHttp3) {
  setup(SetupOpts().setTracing(false));

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
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
  response_encoder_.stream_.codec_callbacks_ = nullptr;
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

  setup(SetupOpts().setTracing(false));
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
  decoder_filters_[0]->callbacks_->recreateStream(nullptr);
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
  OptRef<const Router::ScopeKeyBuilder> scopeKeyBuilder() override {
    return scope_key_builder2_ ? *scope_key_builder2_ : OptRef<const Router::ScopeKeyBuilder>{};
  }

  std::shared_ptr<Router::MockRouteConfigProvider> route_config_provider2_;
  std::shared_ptr<Router::MockScopedRouteConfigProvider> scoped_route_config_provider2_;
  std::unique_ptr<Router::MockScopeKeyBuilder> scope_key_builder2_;
};

// HCM config can only have either RouteConfigProvider or ScopedRoutesConfigProvider.
TEST_F(HttpConnectionManagerImplDeathTest, InvalidConnectionManagerConfig) {
  setup();

  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));
  // Either RDS or SRDS should be set.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or \\(scopedRouteConfigProvider and "
                     "scopeKeyBuilder\\) should be set in ConnectionManagerImpl.");

  route_config_provider2_ = std::make_shared<NiceMock<Router::MockRouteConfigProvider>>();

  // Only route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));

  scoped_route_config_provider2_ =
      std::make_shared<NiceMock<Router::MockScopedRouteConfigProvider>>();
  scope_key_builder2_ = std::make_unique<NiceMock<Router::MockScopeKeyBuilder>>();
  // Can't have RDS and SRDS provider in the same time.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or \\(scopedRouteConfigProvider and "
                     "scopeKeyBuilder\\) should be set in ConnectionManagerImpl.");

  route_config_provider2_.reset();
  // Only scoped route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestRejectedViaIPDetection) {
  OriginalIPRejectRequestOptions reject_options = {Http::Code::Forbidden, "ip detection failed"};
  auto extension = getCustomHeaderExtension("x-ip", reject_options);
  ip_detection_extensions_.push_back(extension);

  use_remote_address_ = false;

  setup();

  // 403 direct response when IP detection fails.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("403", headers.getStatusValue());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  startRequest();

  EXPECT_EQ("ip detection failed", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_rejected_via_ip_detection_.value());
}

TEST_F(HttpConnectionManagerImplTest, DisconnectDuringEncodeHeader) {
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest(/*end_stream=*/true);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
        conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, DisconnectDuringEncodeBody) {
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest(/*end_stream=*/true);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {
        conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, true);
}

// Verify that trailers added during a data encoding continuation are not double continued.
TEST_F(HttpConnectionManagerImplTest, AddTrailersDuringdDecodingContinue) {
  InSequence s;
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl request_body("request");
    decoder_->decodeData(request_body, true);

    return Http::okStatus();
  }));

  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterDataStatus {
        decoder_filters_[1]->callbacks_->addDecodedTrailers().addCopy(LowerCaseString("hello"), 1);
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

// Verify that trailers added during a data decoding continuation are not double continued.
TEST_F(HttpConnectionManagerImplTest, AddTrailersDuringEncodingContinue) {
  InSequence s;
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(1, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterDataStatus {
        encoder_filters_[0]->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("hello"), 1);
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, true);
}

TEST_F(HttpConnectionManagerImplTest, DisconnectDuringEncodeTrailer) {
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest(/*end_stream=*/true);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(response_encoder_, encodeTrailers(_))
      .WillOnce(Invoke([&](const Http::ResponseTrailerMap&) -> void {
        conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, false);
  decoder_filters_[0]->callbacks_->encodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

TEST_F(HttpConnectionManagerImplTest, DirectLocalReplyCausesDisconnect) {
  initial_buffer_limit_ = 10;
  setup();
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
  EXPECT_CALL(response_encoder_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {
        conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);

  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

// Header validator rejects header map for HTTP/1.x protocols
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectHttp1) {
  setup();
  expectUhvHeaderCheck(HeaderValidator::ValidationResult(
                           HeaderValidator::ValidationResult::Action::Reject, "bad_header_map"),
                       ServerHeaderValidator::RequestHeadersTransformationResult::success());

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));
  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillRepeatedly(Return(false));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(*filter, encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        // By default mock codec indicates HTTP/1.1 protocol which should result in closed
        // connection on error
        EXPECT_EQ("close", headers.getConnectionValue());
        EXPECT_EQ("bad_header_map",
                  filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
      }));
  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator rejects header map for HTTP/2 protocols
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectHttp2) {
  codec_->protocol_ = Protocol::Http2;
  setup();
  expectUhvHeaderCheck(HeaderValidator::ValidationResult(
                           HeaderValidator::ValidationResult::Action::Reject, "bad_header_map"),
                       ServerHeaderValidator::RequestHeadersTransformationResult::success());

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        // For HTTP/2 protocols connection should not be closed
        EXPECT_TRUE(headers.Connection() == nullptr);
        EXPECT_EQ("bad_header_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator rejects gRPC request
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectGrpcRequest) {
  codec_->protocol_ = Protocol::Http2;
  setup();
  expectUhvHeaderCheck(HeaderValidator::ValidationResult(
                           HeaderValidator::ValidationResult::Action::Reject, "bad_header_map"),
                       ServerHeaderValidator::RequestHeadersTransformationResult::success());

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {"content-type", "application/grpc"}, // Make Envoy interpret this request as gRPC call
        {":authority", "host"},
        {":path", "/something"},
        {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
        EXPECT_EQ("13", headers.getGrpcStatusValue());
        EXPECT_EQ("bad_header_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator redirects
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRedirect) {
  setup();
  expectUhvHeaderCheck(
      HeaderValidator::ValidationResult::success(),
      ServerHeaderValidator::RequestHeadersTransformationResult(
          ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect,
          "bad_header_map"));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("307", headers.getStatusValue());
        EXPECT_EQ("/some/new/path", headers.getLocationValue());
        EXPECT_EQ("bad_header_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator redirects gRPC request
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRedirectGrpcRequest) {
  codec_->protocol_ = Protocol::Http2;
  setup();
  expectUhvHeaderCheck(
      HeaderValidator::ValidationResult::success(),
      ServerHeaderValidator::RequestHeadersTransformationResult(
          ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect,
          "bad_header_map"));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {"content-type", "application/grpc"}, // Make Envoy interpret this request as gRPC call
        {":authority", "host"},
        {":path", "/something"},
        {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
        EXPECT_EQ("13", headers.getGrpcStatusValue());
        EXPECT_EQ("bad_header_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator rejects trailer map before response has started
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectTrailersBeforeResponseHttp1) {
  codec_->protocol_ = Protocol::Http11;
  setup();
  expectUhvTrailerCheck(HeaderValidator::ValidationResult(
                            HeaderValidator::ValidationResult::Action::Reject, "bad_trailer_map"),
                        HeaderValidator::TransformationResult::success());

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    RequestTrailerMapPtr trailers{
        new TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer2", "value2"}}};
    decoder_->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        EXPECT_EQ("bad_trailer_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectTrailersBeforeResponseHttp2) {
  codec_->protocol_ = Protocol::Http2;
  setup();
  expectUhvTrailerCheck(HeaderValidator::ValidationResult(
                            HeaderValidator::ValidationResult::Action::Reject, "bad_trailer_map"),
                        HeaderValidator::TransformationResult::success(), false);

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    RequestTrailerMapPtr trailers{
        new TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer2", "value2"}}};
    decoder_->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_.stream_, resetStream(_));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, HeaderValidatorFailTrailersTransformationBeforeResponse) {
  codec_->protocol_ = Protocol::Http11;
  setup();
  expectUhvTrailerCheck(
      HeaderValidator::ValidationResult::success(),
      HeaderValidator::TransformationResult(HeaderValidator::TransformationResult::Action::Reject,
                                            "bad_trailer_map"));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    RequestTrailerMapPtr trailers{
        new TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer2", "value2"}}};
    decoder_->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        EXPECT_EQ("bad_trailer_map", decoder_->streamInfo().responseCodeDetails().value());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Header validator rejects trailer map after response has started
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorRejectTrailersAfterResponse) {
  codec_->protocol_ = Protocol::Http2;
  setup();
  setupFilterChain(1, 0, 1);
  expectUhvTrailerCheck(HeaderValidator::ValidationResult(
                            HeaderValidator::ValidationResult::Action::Reject, "bad_trailer_map"),
                        HeaderValidator::TransformationResult::success());
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillRepeatedly(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/something"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "");

    RequestTrailerMapPtr trailers{
        new TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer2", "value2"}}};
    decoder_->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
      }));

  EXPECT_CALL(response_encoder_.stream_, resetStream(_));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  EXPECT_EQ("bad_trailer_map", decoder_->streamInfo().responseCodeDetails().value());
}

// Request completes normally if header validator accepts it
TEST_F(HttpConnectionManagerImplTest, HeaderValidatorAccept) {
  setup();
  expectUhvHeaderCheck(HeaderValidator::ValidationResult::success(),
                       ServerHeaderValidator::RequestHeadersTransformationResult::success());

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, NoProxyProtocolAdded) {
  add_proxy_protocol_connection_state_ = false;
  setup();
  Buffer::OwnedImpl fake_input("input");
  conn_manager_->createCodec(fake_input);

  startRequest(false);

  EXPECT_FALSE(decoder_->streamInfo().filterState()->hasDataWithName(
      Network::ProxyProtocolFilterState::key()));
  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Validate that deferred streams are processed with a variety of
// headers, data, metadata, and trailers arriving in the same I/O cycle
TEST_F(HttpConnectionManagerImplTest, LimitWorkPerIOCycle) {
  const int kRequestsSentPerIOCycle = 100;
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _)).WillRepeatedly(ReturnArg<1>());
  // Process 1 request per I/O cycle
  auto* deferred_request_callback = enableStreamsPerIoLimit(1);
  setup();

  // Store the basic request encoder during filter chain setup.
  std::vector<std::shared_ptr<MockStreamDecoderFilter>> decoder_filters;
  int decode_headers_call_count = 0;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    int mod5 = i % 5;
    std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

    // Each 0th request is headers only
    EXPECT_CALL(*filter, decodeHeaders(_, mod5 == 0 ? true : false))
        .WillRepeatedly(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
          ++decode_headers_call_count;
          return FilterHeadersStatus::StopIteration;
        }));

    // Each 1st request is headers and data only
    // Each 2nd request is headers, data and trailers
    if (mod5 == 1 || mod5 == 2) {
      EXPECT_CALL(*filter, decodeData(_, mod5 == 1 ? true : false))
          .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
    }

    // Each 3rd request is headers and trailers (no data)
    if (mod5 == 2 || mod5 == 3) {
      EXPECT_CALL(*filter, decodeTrailers(_)).WillOnce(Return(FilterTrailersStatus::StopIteration));
    }

    // Each 4th request is headers, metadata, and data.
    if (mod5 == 4) {
      EXPECT_CALL(*filter, decodeMetadata(_)).WillOnce(Return(FilterMetadataStatus::Continue));
      EXPECT_CALL(*filter, decodeData(_, true))
          .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
    }
    EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
    decoder_filters.push_back(std::move(filter));
  }

  uint64_t random_value = 0;
  EXPECT_CALL(random_, random()).WillRepeatedly(Invoke([&random_value]() {
    return random_value++;
  }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .Times(kRequestsSentPerIOCycle)
      .WillRepeatedly(Invoke([&decoder_filters](FilterChainManager& manager) -> bool {
        static int index = 0;
        int i = index++;
        FilterFactoryCb factory([&decoder_filters, i](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamDecoderFilter(decoder_filters[i]);
        });
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_))
      .Times(kRequestsSentPerIOCycle);

  std::vector<NiceMock<MockResponseEncoder>> response_encoders(kRequestsSentPerIOCycle);
  for (auto& encoder : response_encoders) {
    EXPECT_CALL(encoder, getStream()).WillRepeatedly(ReturnRef(encoder.stream_));
  }

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
          decoder_ = &conn_manager_->newStream(response_encoders[i]);

          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

          MetadataMapPtr metadata = std::make_unique<MetadataMap>();
          (*metadata)["key1"] = "value1";

          RequestTrailerMapPtr trailers{
              new TestRequestTrailerMapImpl{{"key1", "value1"}, {"key2", "value2"}}};

          Buffer::OwnedImpl data("data");

          switch (i % 5) {
          case 0:
            decoder_->decodeHeaders(std::move(headers), true);
            break;
          case 1:
            decoder_->decodeHeaders(std::move(headers), false);
            decoder_->decodeData(data, true);
            break;
          case 2:
            decoder_->decodeHeaders(std::move(headers), false);
            decoder_->decodeData(data, false);
            decoder_->decodeTrailers(std::move(trailers));
            break;
          case 3:
            decoder_->decodeHeaders(std::move(headers), false);
            decoder_->decodeTrailers(std::move(trailers));
            break;
          case 4:
            decoder_->decodeHeaders(std::move(headers), false);
            decoder_->decodeMetadata(std::move(metadata));
            decoder_->decodeData(data, true);
            break;
          }
        }

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_TRUE(deferred_request_callback->enabled_);
  // Only one request should go through the filter chain
  ASSERT_EQ(decode_headers_call_count, 1);

  // Let other requests to go through the filter chain. Call expectations will fail
  // if this is not the case.
  int deferred_request_count = 0;
  while (deferred_request_callback->enabled_) {
    deferred_request_callback->invokeCallback();
    ++deferred_request_count;
  }

  ASSERT_EQ(deferred_request_count, kRequestsSentPerIOCycle);

  for (auto& filter : decoder_filters) {
    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
  }

  EXPECT_EQ(kRequestsSentPerIOCycle, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(kRequestsSentPerIOCycle, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(kRequestsSentPerIOCycle, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(kRequestsSentPerIOCycle, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, StreamDeferralPreservesOrder) {
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _)).WillRepeatedly(ReturnArg<1>());
  // Process 1 request per I/O cycle
  auto* deferred_request_callback = enableStreamsPerIoLimit(1);
  setup();

  std::vector<std::shared_ptr<MockStreamDecoderFilter>> encoder_filters;
  int expected_request_id = 0;
  const Http::LowerCaseString request_id_header(absl::string_view("request-id"));
  // Two requests are processed in 2 I/O reads
  const int TotalRequests = 2 * 2;
  for (int i = 0; i < TotalRequests; ++i) {
    std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

    EXPECT_CALL(*filter, decodeHeaders(_, true))
        .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
          // Check that requests are decoded in expected order
          int request_id = 0;
          ASSERT(absl::SimpleAtoi(headers.get(request_id_header)[0]->value().getStringView(),
                                  &request_id));
          ASSERT(request_id == expected_request_id);
          ++expected_request_id;
          return FilterHeadersStatus::StopIteration;
        }));

    EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
    encoder_filters.push_back(std::move(filter));
  }

  uint64_t random_value = 0;
  EXPECT_CALL(random_, random()).WillRepeatedly(Invoke([&random_value]() {
    return random_value++;
  }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .Times(TotalRequests)
      .WillRepeatedly(Invoke([&encoder_filters](FilterChainManager& manager) -> bool {
        static int index = 0;
        int i = index++;
        FilterFactoryCb factory([&encoder_filters, i](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamDecoderFilter(encoder_filters[i]);
        });
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(TotalRequests);

  std::vector<NiceMock<MockResponseEncoder>> response_encoders(TotalRequests);
  for (auto& encoder : response_encoders) {
    EXPECT_CALL(encoder, getStream()).WillRepeatedly(ReturnRef(encoder.stream_));
  }
  auto response_encoders_iter = response_encoders.begin();

  int request_id = 0;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        // The second request should be deferred
        for (int i = 0; i < 2; ++i) {
          decoder_ = &conn_manager_->newStream(*response_encoders_iter);
          ++response_encoders_iter;

          RequestHeaderMapPtr headers{
              new TestRequestHeaderMapImpl{{":authority", "host"},
                                           {":path", "/"},
                                           {":method", "GET"},
                                           {"request-id", absl::StrCat(request_id)}}};

          ++request_id;
          decoder_->decodeHeaders(std::move(headers), true);
        }

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_TRUE(deferred_request_callback->enabled_);
  // Only one request should go through the filter chain
  ASSERT_EQ(expected_request_id, 1);

  // Test arrival of another request. New request is read from the socket before deferred callbacks.
  Buffer::OwnedImpl fake_input2("1234");
  conn_manager_->onData(fake_input2, false);

  // No requests from the second read should go through as there are deferred stream present
  ASSERT_EQ(expected_request_id, 1);

  // Let other requests to go through the filter chain. Call expectations will fail
  // if this is not the case.
  int deferred_request_count = 0;
  while (deferred_request_callback->enabled_) {
    deferred_request_callback->invokeCallback();
    ++deferred_request_count;
  }

  ASSERT_EQ(deferred_request_count, TotalRequests);

  for (auto& filter : encoder_filters) {
    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
  }

  EXPECT_EQ(TotalRequests, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(TotalRequests, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(TotalRequests, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(TotalRequests, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, DownstreamTimingsRecordWhenRequestHeaderProcessingIsDone) {
  setup(SetupOpts().setSsl(true).setTracing(false));

  // Set up the codec.
  Buffer::OwnedImpl fake_input("input");
  conn_manager_->createCodec(fake_input);

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    // Set time to 5ms before creating the stream
    test_time_.timeSystem().setMonotonicTime(MonotonicTime(std::chrono::milliseconds(5)));
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    // Advanced time to 20ms before decoding headers.
    test_time_.timeSystem().setMonotonicTime(MonotonicTime(std::chrono::milliseconds(20)));
    decoder_->decodeHeaders(std::move(headers), /*end_stream=*/false);
    return Http::okStatus();
  }));

  conn_manager_->onData(fake_input, /*end_stream=*/false);

  auto request_headers_done =
      decoder_->streamInfo().downstreamTiming().lastDownstreamHeaderRxByteReceived();
  auto request_headers_done_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
      request_headers_done.value() - decoder_->streamInfo().startTimeMonotonic());
  // Expect time to be 20ms-5ms = 15ms.
  EXPECT_EQ(request_headers_done_millis, std::chrono::milliseconds(15));

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, PassMatchUpstreamSchemeHintToStreamInfo) {
  setup(SetupOpts().setTracing(false));
  scheme_match_upstream_ = true;

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        EXPECT_TRUE(filter->callbacks_->streamInfo().shouldSchemeMatchUpstream());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    // Send request to be passed on through filter chain to check that
    // filters have the correct hint passed through the callbacks
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input{};
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Validate that incomplete request is terminated when a non terminal filter
// initiates encoding of the response (i.e. the cache filter).
// This only works when independent half-close mode is DISABLED.
TEST_F(HttpConnectionManagerImplTest, EncodingByNonTerminalFilter) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false"}});
  setup();
  constexpr int total_filters = 3;
  constexpr int ecoder_filter_index = 1;
  setupFilterChain(total_filters, total_filters);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Kick off the incomplete request (end_stream == false).
  startRequest(false);

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  // Second decoder filter (there are 3 in total) initiates encoding
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeHeaders(std::move(response_headers),
                                                                   false, "details");

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  // verify that after the end_stream is observed by the last encoder filter the request is
  // completed.
  expectOnDestroy();

  // Second decoder filter then completes encoding with data
  Buffer::OwnedImpl fake_response("world");
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeData(fake_response, true);
}

// Validate that when independent half-close is enabled, encoding end_stream by a
// non-final filter ends the request iff the filter that initiated encoding of the end_stream has
// already observed the request end_stream.
TEST_F(HttpConnectionManagerImplTest, EncodingByNonTerminalFilterWithIndependentHalfClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true"}});
  setup();
  constexpr int total_filters = 3;
  constexpr int ecoder_filter_index = 1;
  setupFilterChain(total_filters, total_filters);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Send complete request.
  startRequest(true);

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  // Second decoder filter (there are 3 in total) initiates encoding
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeHeaders(std::move(response_headers),
                                                                   false, "details");

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  // Verify that after the end_stream is observed by the last encoder filter the request is
  // completed. even though the request end_stream never reached the terminal filter, but was
  // observed by the filter that has initiated encoding.
  expectOnDestroy();

  // Second decoder filter then completes encoding with data
  Buffer::OwnedImpl fake_response("world");
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeData(fake_response, true);
}

// Validate that when independent half-close is enabled, encoding end_stream by a
// non-final filter with incomplete request causes the request to be reset.
// Only the terminal filter (router) is allowed to half-close upstream response before the
// downstream request.
TEST_F(HttpConnectionManagerImplTest, DecodingByNonTerminalEncoderFilterWithIndependentHalfClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true"}});
  setup();
  constexpr int total_filters = 3;
  constexpr int ecoder_filter_index = 1;
  setupFilterChain(total_filters, total_filters);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Send incomplete request.
  startRequest(false);

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  // Second decoder filter (there are 3 in total) initiates encoding
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeHeaders(std::move(response_headers),
                                                                   false, "details");

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());

  expectOnDestroy();

  // Second decoder filter then completes encoding with data
  Buffer::OwnedImpl fake_response("world");
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeData(fake_response, true);
}

// Validate that when independent half-close is enabled, encoding end_stream by a
// non-final filter with incomplete request makes the encoding filter the terminal filter.
// In this case decoding end_stream from the client only reaches the filter that encoded the
// end_stream after which the request is completed.
TEST_F(HttpConnectionManagerImplTest, DecodingWithAddedTrailersByNonTerminalEncoderFilter) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true"}});
  setup();
  constexpr int total_filters = 3;
  constexpr int ecoder_filter_index = 1;
  setupFilterChain(total_filters, total_filters);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Send incomplete request.
  startRequest(false);

  // For encode direction
  EXPECT_CALL(*encoder_filters_[2], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));

  // Second decoder filter (there are 3 in total) initiates encoding
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeHeaders(std::move(response_headers),
                                                                   false, "details");

  EXPECT_CALL(*encoder_filters_[2], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[2], encodeComplete());
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  expectOnDestroy();

  // Second decoder filter then completes encoding with data
  Buffer::OwnedImpl fake_response("world");
  decoder_filters_[ecoder_filter_index]->callbacks_->encodeData(fake_response, true);
}

} // namespace Http
} // namespace Envoy
