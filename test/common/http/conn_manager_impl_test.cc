#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/access_log.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/thread.h"
#include "common/http/access_log/access_log_impl.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/exception.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::Test;

namespace Http {

class HttpConnectionManagerImplTest : public Test, public ConnectionManagerConfig {
public:
  HttpConnectionManagerImplTest()
      : access_log_path_("dummy_path"),
        access_logs_{Http::AccessLog::InstancePtr{new Http::AccessLog::InstanceImpl(
            access_log_path_, api_, {},
            std::move(AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter()), dispatcher_,
            lock_, fake_stats_)}},
        codec_(new NiceMock<Http::MockServerConnection>()),
        stats_{{ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_TIMER(fake_stats_))},
               "",
               fake_stats_} {
    tracing_config_.value({"operation", Http::TracingType::All});
  }

  ~HttpConnectionManagerImplTest() {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void setup(bool ssl, const std::string& server_name) {
    if (ssl) {
      ssl_connection_.reset(new Ssl::MockConnection());
    }

    server_name_ = server_name;
    ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_.get()));
    conn_manager_.reset(new ConnectionManagerImpl(*this, drain_close_, random_, tracer_, runtime_));
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  // Http::ConnectionManagerConfig
  const std::list<Http::AccessLog::InstancePtr>& accessLogs() override { return access_logs_; }
  ServerConnectionPtr createCodec(Network::Connection&, const Buffer::Instance&,
                                  ServerConnectionCallbacks&) override {
    return ServerConnectionPtr{codec_};
  }
  std::chrono::milliseconds drainTimeout() override { return std::chrono::milliseconds(100); }
  FilterChainFactory& filterFactory() override { return filter_factory_; }
  bool generateRequestId() override { return true; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  const Router::Config& routeConfig() override { return route_config_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  const std::string& localAddress() override { return local_address_; }
  const Optional<std::string>& userAgent() override { return user_agent_; }
  const Optional<Http::TracingConnectionManagerConfig>& tracingConfig() override {
    return tracing_config_;
  }

  NiceMock<Tracing::MockHttpTracer> tracer_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Api::MockApi> api_;
  Event::MockDispatcher dispatcher_;
  std::string access_log_path_;
  Thread::MutexBasicLockable lock_;
  std::list<Http::AccessLog::InstancePtr> access_logs_;
  Stats::IsolatedStoreImpl fake_stats_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  Http::MockServerConnection* codec_;
  NiceMock<Http::MockFilterChainFactory> filter_factory_;
  ConnectionManagerStats stats_;
  NiceMock<Network::MockDrainDecision> drain_close_;
  std::unique_ptr<ConnectionManagerImpl> conn_manager_;
  std::string server_name_;
  std::string local_address_;
  bool use_remote_address_{true};
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<Ssl::MockConnection> ssl_connection_;
  NiceMock<Router::MockConfig> route_config_;
  Optional<Http::TracingConnectionManagerConfig> tracing_config_;
};

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(false, "envoy-custom-server");

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<Http::MockStreamDecoderFilter> filter(
      new NiceMock<Http::MockStreamDecoderFilter>());

  EXPECT_CALL(filter->reset_stream_called_, ready()).Times(0);
  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .Times(2)
      .WillRepeatedly(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_STREQ("http", headers.ForwardedProto()->value().c_str());
        if (headers.Path()->value() == "/healthcheck") {
          filter->callbacks_->requestInfo().healthCheck(true);
        }

        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_)).Times(2);

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks)
                                 -> void { callbacks.addStreamDecoderFilter(filter); }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  Http::StreamDecoder* decoder = nullptr;
  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        if (data.length() == 4) {
          Http::HeaderMapPtr headers{new TestHeaderMapImpl{
              {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
          decoder->decodeHeaders(std::move(headers), true);
        } else {
          Http::HeaderMapPtr headers{new TestHeaderMapImpl{
              {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/healthcheck"}}};
          decoder->decodeHeaders(std::move(headers), true);
        }

        Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        // Drain 2 so that on the 2nd iteration we will hit zero.
        data.drain(2);
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, InvalidPath) {
  setup(false, "");
  EXPECT_CALL(tracer_, trace(_, _, _, _));

  Http::StreamDecoder* decoder = nullptr;
  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "http://api.lyft.com/"}}};
        decoder->decodeHeaders(std::move(headers), true);
        data.drain(4);
      }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const Http::HeaderMap& headers, bool)
                           -> void { EXPECT_STREQ("404", headers.Status()->value().c_str()); }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, DrainClose) {
  setup(true, "");
  EXPECT_CALL(tracer_, trace(_, _, _, _));

  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_STREQ("https", headers.ForwardedProto()->value().c_str());
        return FilterHeadersStatus::StopIteration;
      }));

  Http::StreamDecoder* decoder = nullptr;
  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input);

  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->callback_();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  setup(false, "envoy-server-test");
  // Make tracing off.
  tracing_config_ = Optional<Http::TracingConnectionManagerConfig>();
  EXPECT_CALL(tracer_, trace(_, _, _, _)).Times(0);

  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));

  Http::StreamDecoder* decoder = nullptr;
  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input);

  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(filter->reset_stream_called_, ready());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const Http::HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_STREQ("envoy-server-test", headers.Server()->value().c_str());
      }));

  filter->callbacks_->encodeHeaders(std::move(response_headers), true);
}

TEST_F(HttpConnectionManagerImplTest, ResponseStartBeforeRequestComplete) {
  setup(false, "");

  // This is like ResponseBeforeRequestComplete, but it tests the case where we start the reply
  // before the request completes, but don't finish the reply until after the request completes.
  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));

  // Start the request
  Http::StreamDecoder* decoder = nullptr;
  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input);

  // Start the response
  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([](const Http::HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_STREQ("", headers.Server()->value().c_str());
      }));
  filter->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Finish the request.
  EXPECT_CALL(*filter, decodeData(_, true));
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void { decoder->decodeData(data, true); }));

  conn_manager_->onData(fake_input);

  // Since we started the response before the request was complete, we will still close the
  // connection since we already sent a connection: close header. We won't "reset" the stream
  // however.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  Buffer::OwnedImpl fake_response("world");
  filter->callbacks_->encodeData(fake_response, true);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamDisconnect) {
  setup(false, "");

  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void {
        conn_manager_->newStream(encoder);
        data.drain(2);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Now raise a remote disconnection, we should see the filter get reset called.
  InSequence s;
  EXPECT_CALL(filter->reset_stream_called_, ready());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamProtocolError) {
  setup(false, "");

  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  // A protocol exception should result in reset of the streams followed by a local close.
  Sequence s;
  EXPECT_CALL(filter->reset_stream_called_, ready()).InSequence(s);
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .InSequence(s);

  NiceMock<Http::MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        conn_manager_->newStream(encoder);
        throw CodecProtocolException("protocol error");
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeoutNoCodec) {
  // Not used in the test.
  delete codec_;

  idle_timeout_.value(std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_));
  setup(false, "");

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->callback_();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, IdleTimeout) {
  idle_timeout_.value(std::chrono::milliseconds(10));
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_));
  setup(false, "");

  Http::MockStreamDecoderFilter* filter = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{filter});
      }));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, true);
      }));

  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationNoBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_CALL(*idle_timer, enableTimer(_));
  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);

  Event::MockTimer* drain_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  idle_timer->callback_();

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->callback_();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, IntermediateBufferingEarlyResponse) {
  setup(false, "");

  Http::MockStreamDecoderFilter* decoder_filter1 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter2 = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter1});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter2});
      }));

  EXPECT_CALL(*decoder_filter1, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter1, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, true);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Mimic a decoder filter that trapped data and now sends on the headers.
  EXPECT_CALL(*decoder_filter2, decodeHeaders(_, false))
      .WillOnce(Invoke([&](Http::HeaderMap&, bool) -> Http::FilterHeadersStatus {
        // Now filter 2 will send a complete response.
        Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        decoder_filter2->callbacks_->encodeHeaders(std::move(response_headers), true);
        return Http::FilterHeadersStatus::StopIteration;
      }));

  // Response is already complete so we drop buffered body data when we continue.
  EXPECT_CALL(*decoder_filter2, decodeData(_, _)).Times(0);
  decoder_filter1->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, DoubleBuffering) {
  setup(false, "");

  Http::MockStreamDecoderFilter* decoder_filter1 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter2 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter3 = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter1});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter2});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter3});
      }));

  EXPECT_CALL(*decoder_filter1, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter1, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* decoder = nullptr;

  // The data will get moved so we need to have a copy to compare against.
  Buffer::OwnedImpl fake_data("hello");
  Buffer::OwnedImpl fake_data_copy("hello");
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
        decoder->decodeData(fake_data, true);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Continue iteration and stop and buffer on the 2nd filter.
  EXPECT_CALL(*decoder_filter2, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter2, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));
  decoder_filter1->callbacks_->continueDecoding();

  // Continue iteration. We expect the 3rd filter to not receive double data but for the buffered
  // data to have been kept inline as it moves through.
  EXPECT_CALL(*decoder_filter3, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter3, decodeData(BufferEqual(&fake_data_copy), true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationNoBuffer));
  decoder_filter2->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, ZeroByteDataFiltering) {
  setup(false, "");

  Http::MockStreamDecoderFilter* decoder_filter1 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter2 = new NiceMock<Http::MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter1});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter2});
      }));

  EXPECT_CALL(*decoder_filter1, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Continue headers only of filter 1.
  EXPECT_CALL(*decoder_filter2, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  decoder_filter1->callbacks_->continueDecoding();

  // Stop zero byte data.
  EXPECT_CALL(*decoder_filter1, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));
  Buffer::OwnedImpl zero;
  decoder->decodeData(zero, true);

  // Continue.
  EXPECT_CALL(*decoder_filter2, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationNoBuffer));
  decoder_filter1->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, MultipleFilters) {
  setup(false, "");

  Http::MockStreamDecoderFilter* decoder_filter1 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter2 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamDecoderFilter* decoder_filter3 = new NiceMock<Http::MockStreamDecoderFilter>();
  Http::MockStreamEncoderFilter* encoder_filter1 = new NiceMock<Http::MockStreamEncoderFilter>();
  Http::MockStreamEncoderFilter* encoder_filter2 = new NiceMock<Http::MockStreamEncoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter1});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter2});
        callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{decoder_filter3});
        callbacks.addStreamEncoderFilter(Http::StreamEncoderFilterPtr{encoder_filter1});
        callbacks.addStreamEncoderFilter(Http::StreamEncoderFilterPtr{encoder_filter2});
      }));

  EXPECT_CALL(*decoder_filter1, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter1, decodeData(_, false))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filter1, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        Http::HeaderMapPtr headers{new TestHeaderMapImpl{
            {":version", "HTTP/1.1"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, false);

        Buffer::OwnedImpl fake_data2("world");
        decoder->decodeData(fake_data2, true);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Mimic a decoder filter that trapped data and now sends it on, since the data was buffered
  // by the first filter, we expect to get it in 1 decodeData() call.
  EXPECT_CALL(*decoder_filter2, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filter2, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filter3, decodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter3, decodeData(_, true))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationNoBuffer));
  decoder_filter1->callbacks_->continueDecoding();

  // Now start encoding and mimic trapping in the encoding filter.
  EXPECT_CALL(*encoder_filter1, encodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filter1, encodeData(_, false))
      .WillOnce(Return(Http::FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*encoder_filter1, encodeTrailers(_))
      .WillOnce(Return(Http::FilterTrailersStatus::StopIteration));
  decoder_filter3->callbacks_->encodeHeaders(
      Http::HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl response_body("response");
  decoder_filter3->callbacks_->encodeData(response_body, false);
  decoder_filter3->callbacks_->encodeTrailers(
      Http::HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});

  // Now finish the encode.
  EXPECT_CALL(*encoder_filter2, encodeHeaders(_, false))
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(encoder, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filter2, encodeData(_, false))
      .WillOnce(Return(Http::FilterDataStatus::Continue));
  EXPECT_CALL(encoder, encodeData(_, false));
  EXPECT_CALL(*encoder_filter2, encodeTrailers(_))
      .WillOnce(Return(Http::FilterTrailersStatus::Continue));
  EXPECT_CALL(encoder, encodeTrailers(_));
  encoder_filter1->callbacks_->continueEncoding();
}

} // Http
