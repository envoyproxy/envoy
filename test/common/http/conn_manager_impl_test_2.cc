#include <chrono>

#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/extensions/filters/network/common/fuzz/utils/fakes.h"
#include "test/server/utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Ref;
using testing::Return;
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto filter_factory = createDecoderFilterFactoryCb(filter);
        auto handler_factory = createLogHandlerFactoryCb(handler);

        callbacks.setFilterConfigName("");
        filter_factory(callbacks);
        handler_factory(callbacks);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
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
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
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
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        callbacks.setFilterConfigName("");
        factory(callbacks);
        return true;
      }));

  auto mock_route_0 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(Router::VirtualHostRoute{mock_route_0->virtual_host_, mock_route_0}));

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
      .WillOnce(Return(Router::VirtualHostRoute{mock_route_1->virtual_host_, mock_route_1}));
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

  std::shared_ptr<Router::MockVirtualHost> mock_virtual_host =
      std::make_shared<NiceMock<Router::MockVirtualHost>>();

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(Router::VirtualHostRoute{route1->virtual_host_, route1}))
      .WillOnce(Return(Router::VirtualHostRoute{route2->virtual_host_, route2}))
      .WillOnce(Return(Router::VirtualHostRoute{mock_virtual_host, nullptr}))
      .WillOnce(Return(Router::VirtualHostRoute{}));

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
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->clusterInfo());

        // Null route but the virtual host is set.
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->streamInfo().route());
        EXPECT_EQ(mock_virtual_host, decoder_filters_[2]->callbacks_->streamInfo().virtualHost());

        // Clear route cache again.
        decoder_filters_[2]->callbacks_->downstreamCallbacks()->clearRouteCache();

        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->clusterInfo());

        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->streamInfo().route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->streamInfo().virtualHost());

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
      .WillOnce(Return(Router::VirtualHostRoute{route1->virtual_host_, route1}));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(fake_cluster1->info(), decoder_filters_[0]->callbacks_->clusterInfo());

        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->streamInfo().route());
        EXPECT_EQ(route1->virtual_host_,
                  decoder_filters_[1]->callbacks_->streamInfo().virtualHost());

        decoder_filters_[0]->callbacks_->downstreamCallbacks()->setRoute(nullptr);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->clusterInfo());

        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->streamInfo().route());
        EXPECT_EQ(nullptr, decoder_filters_[1]->callbacks_->streamInfo().virtualHost());

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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
  ResponseHeaderMap* original_response_headers = response_headers.get();

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
        // The new headers should overwrite the original headers.
        EXPECT_NE(&headers, original_response_headers);
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

  // The active stream will keep the overwritten headers alive to avoid potential lifetime issues.
  // Ensure the original headers are still valid.
  EXPECT_EQ(original_response_headers->getStatusValue(), "200");

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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
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

} // namespace Http
} // namespace Envoy
