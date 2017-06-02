#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/access_log.h"
#include "envoy/tracing/http_tracer.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/access_log/access_log_impl.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::Test;

namespace Http {

class HttpConnectionManagerImplTest : public Test, public ConnectionManagerConfig {
public:
  struct RouteConfigProvider : public Router::RouteConfigProvider {
    // Router::RouteConfigProvider
    Router::ConfigConstSharedPtr config() override { return route_config_; }

    std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  };

  HttpConnectionManagerImplTest()
      : access_log_path_("dummy_path"),
        access_logs_{AccessLog::InstanceSharedPtr{new AccessLog::InstanceImpl(
            access_log_path_, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
            log_manager_)}},
        codec_(new NiceMock<MockServerConnection>()),
        stats_{{ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_TIMER(fake_stats_))},
               "",
               fake_stats_},
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))} {
    tracing_config_.reset(new TracingConnectionManagerConfig(
        {Tracing::OperationName::Ingress, {LowerCaseString(":method")}}));

    // response_encoder_ is not a NiceMock on purpose. This prevents complaining about this
    // method only.
    EXPECT_CALL(response_encoder_, getStream()).Times(AtLeast(0));
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
    ON_CALL(filter_callbacks_.connection_, remoteAddress())
        .WillByDefault(ReturnRef(remote_address_));
    conn_manager_.reset(new ConnectionManagerImpl(*this, drain_close_, random_, tracer_, runtime_));
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  void setupFilterChain(int num_decoder_filters, int num_encoder_filters) {
    // NOTE: The length/repetition in this routine allows InSequence to work correctly in an outer
    // scope.
    for (int i = 0; i < num_decoder_filters; i++) {
      decoder_filters_.push_back(new MockStreamDecoderFilter());
    }

    for (int i = 0; i < num_encoder_filters; i++) {
      encoder_filters_.push_back(new MockStreamEncoderFilter());
    }

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([num_decoder_filters, num_encoder_filters, this](
                             FilterChainFactoryCallbacks& callbacks) -> void {
          for (int i = 0; i < num_decoder_filters; i++) {
            callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{decoder_filters_[i]});
          }

          for (int i = 0; i < num_encoder_filters; i++) {
            callbacks.addStreamEncoderFilter(StreamEncoderFilterSharedPtr{encoder_filters_[i]});
          }
        }));

    for (int i = 0; i < num_decoder_filters; i++) {
      EXPECT_CALL(*decoder_filters_[i], setDecoderFilterCallbacks(_));
    }

    for (int i = 0; i < num_encoder_filters; i++) {
      EXPECT_CALL(*encoder_filters_[i], setEncoderFilterCallbacks(_));
    }
  }

  void expectOnDestroy() {
    for (auto filter : decoder_filters_) {
      EXPECT_CALL(*filter, onDestroy());
    }

    for (auto filter : encoder_filters_) {
      EXPECT_CALL(*filter, onDestroy());
    }

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  }

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  ServerConnectionPtr createCodec(Network::Connection&, const Buffer::Instance&,
                                  ServerConnectionCallbacks&) override {
    return ServerConnectionPtr{codec_};
  }
  DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return std::chrono::milliseconds(100); }
  FilterChainFactory& filterFactory() override { return filter_factory_; }
  bool generateRequestId() override { return true; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return route_config_provider_; }
  const std::string& serverName() override { return server_name_; }
  ConnectionManagerStats& stats() override { return stats_; }
  ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  const Network::Address::Instance& localAddress() override { return local_address_; }
  const Optional<std::string>& userAgent() override { return user_agent_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }

  NiceMock<Tracing::MockHttpTracer> tracer_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  std::string access_log_path_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  Stats::IsolatedStoreImpl fake_stats_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  MockServerConnection* codec_;
  NiceMock<MockFilterChainFactory> filter_factory_;
  ConnectionManagerStats stats_;
  ConnectionManagerTracingStats tracing_stats_;
  NiceMock<Network::MockDrainDecision> drain_close_;
  std::unique_ptr<ConnectionManagerImpl> conn_manager_;
  std::string server_name_;
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  Network::Address::Ipv4Instance remote_address_{"0.0.0.0"};
  bool use_remote_address_{true};
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<Ssl::MockConnection> ssl_connection_;
  RouteConfigProvider route_config_provider_;
  TracingConnectionManagerConfigPtr tracing_config_;
  SlowDateProviderImpl date_provider_;

  // TODO(mattklein123): Not all tests have been converted over to better setup. Convert the rest.
  MockStreamEncoder response_encoder_;
  std::vector<MockStreamDecoderFilter*> decoder_filters_;
  std::vector<MockStreamEncoderFilter*> encoder_filters_;
};

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(false, "envoy-custom-server");

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

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
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks)
                                 -> void { callbacks.addStreamDecoderFilter(filter); }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        if (data.length() == 4) {
          HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
          decoder->decodeHeaders(std::move(headers), true);
        } else {
          HeaderMapPtr headers{
              new TestHeaderMapImpl{{":authority", "host"}, {":path", "/healthcheck"}}};
          decoder->decodeHeaders(std::move(headers), true);
        }

        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        // Drain 2 so that on the 2nd iteration we will hit zero.
        data.drain(2);
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{
            new TestHeaderMapImpl{{":authority", "host"}, {":path", "http://api.lyft.com/"}}};
        decoder->decodeHeaders(std::move(headers), true);
        data.drain(4);
      }));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  MockStreamFilter* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks)
                           -> void { callbacks.addStreamFilter(StreamFilterSharedPtr{filter}); }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool)
                           -> void { EXPECT_STREQ("404", headers.Status()->value().c_str()); }));
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const AccessLog::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

        return span;
      }));
  EXPECT_CALL(*span, finishSpan(_))
      .WillOnce(
          Invoke([&](Tracing::SpanFinalizer& finalizer) -> void { finalizer.finalize(*span); }));
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  // Verify tag is set based on the request headers.
  EXPECT_CALL(*span, setTag(":method", "GET"));
  // Verify if the activeSpan interface returns reference to the current span.
  EXPECT_CALL(*span, setTag("service-cluster", "scoobydoo"));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks)
                                 -> void { callbacks.addStreamDecoderFilter(filter); }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        HeaderMapPtr headers{
            new TestHeaderMapImpl{{":method", "GET"},
                                  {":authority", "host"},
                                  {":path", "/"},
                                  {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_EQ(1UL, tracing_stats_.service_forced_.value());
  EXPECT_EQ(0UL, tracing_stats_.random_sampling_.value());

  // Uncomment either of these lines and the test fails
  //   EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(span));
  //   delete span;
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLog) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _))
      .WillOnce(Invoke(
          [](const HeaderMap*, const HeaderMap*, const AccessLog::RequestInfo& request_info) {
            EXPECT_TRUE(request_info.responseCode().valid());
            EXPECT_EQ(request_info.responseCode().value(), uint32_t(200));
          }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        HeaderMapPtr headers{
            new TestHeaderMapImpl{{":method", "GET"},
                                  {":authority", "host"},
                                  {":path", "/"},
                                  {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, DoNotStartSpanIfTracingIsNotEnabled) {
  setup(false, "");

  // Disable tracing.
  tracing_config_.reset();

  EXPECT_CALL(tracer_, startSpan_(_, _, _)).Times(0);
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillByDefault(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks)
                                 -> void { callbacks.addStreamDecoderFilter(filter); }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        HeaderMapPtr headers{
            new TestHeaderMapImpl{{":method", "GET"},
                                  {":authority", "host"},
                                  {":path", "/"},
                                  {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, StartSpanOnlyHealthCheckRequest) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();

  EXPECT_CALL(tracer_, startSpan_(_, _, _)).WillOnce(Return(span));
  EXPECT_CALL(*span, finishSpan(_)).Times(0);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks)
                                 -> void { callbacks.addStreamDecoderFilter(filter); }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](HeaderMap&, bool) -> FilterHeadersStatus {
        filter->callbacks_->requestInfo().healthCheck(true);
        return FilterHeadersStatus::StopIteration;
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        HeaderMapPtr headers{
            new TestHeaderMapImpl{{":method", "GET"},
                                  {":authority", "host"},
                                  {":path", "/healthcheck"},
                                  {"x-request-id", "125a4afb-6f55-94ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Force dtor of active stream to be called so that we can capture tracing HC stat.
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  // HC request, but was originally sampled, so check for two stats here.
  EXPECT_EQ(1UL, tracing_stats_.random_sampling_.value());
  EXPECT_EQ(1UL, tracing_stats_.health_check_.value());
  EXPECT_EQ(0UL, tracing_stats_.service_forced_.value());
}

TEST_F(HttpConnectionManagerImplTest, NoPath) {
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
        decoder->decodeHeaders(std::move(headers), true);
        data.drain(4);
      }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool)
                           -> void { EXPECT_STREQ("404", headers.Status()->value().c_str()); }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);
}

TEST_F(HttpConnectionManagerImplTest, DrainClose) {
  setup(true, "");

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_STREQ("https", headers.ForwardedProto()->value().c_str());
        return FilterHeadersStatus::StopIteration;
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input);

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);
  EXPECT_EQ(ssl_connection_.get(), filter->callbacks_->ssl());

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->callback_();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  InSequence s;
  setup(false, "envoy-server-test");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_STREQ("envoy-server-test", headers.Server()->value().c_str());
      }));
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true);
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
  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input);

  // Start the response
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
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
  InSequence s;
  setup(false, "");

  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> void {
        conn_manager_->newStream(encoder);
        data.drain(2);
      }));

  setupFilterChain(1, 0);

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Now raise a remote disconnection, we should see the filter get reset called.
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamProtocolError) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        conn_manager_->newStream(response_encoder_);
        throw CodecProtocolException("protocol error");
      }));

  setupFilterChain(1, 0);

  // A protocol exception should result in reset of the streams followed by a local close.
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

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

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  NiceMock<MockStreamEncoder> encoder;
  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(encoder);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, true);
      }));

  EXPECT_CALL(*idle_timer, disableTimer());
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_CALL(*idle_timer, enableTimer(_));
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
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
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, true);
      }));

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Mimic a decoder filter that trapped data and now sends on the headers.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([&](HeaderMap&, bool) -> FilterHeadersStatus {
        // Now filter 2 will send a complete response.
        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        decoder_filters_[1]->callbacks_->encodeHeaders(std::move(response_headers), true);
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();

  // Response is already complete so we drop buffered body data when we continue.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, _)).Times(0);
  decoder_filters_[0]->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, DoubleBuffering) {
  InSequence s;
  setup(false, "");

  // The data will get moved so we need to have a copy to compare against.
  Buffer::OwnedImpl fake_data("hello");
  Buffer::OwnedImpl fake_data_copy("hello");
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
        decoder->decodeData(fake_data, true);
      }));

  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Continue iteration and stop and buffer on the 2nd filter.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Continue iteration. We expect the 3rd filter to not receive double data but for the buffered
  // data to have been kept inline as it moves through.
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[2], decodeData(BufferEqual(&fake_data_copy), true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[1]->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, ZeroByteDataFiltering) {
  InSequence s;
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);
      }));

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Continue headers only of filter 1.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Stop zero byte data.
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  Buffer::OwnedImpl zero;
  decoder->decodeData(zero, true);

  // Continue.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[0]->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, false);

        HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
        decoder->decodeTrailers(std::move(trailers));
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
        decoder_filters_[0]->callbacks_->addDecodedData(trailers_data);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(Ref(trailers_data), false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));

  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, false);
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        encoder_filters_[0]->callbacks_->addEncodedData(trailers_data);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeData(Ref(trailers_data), false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInline) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);
      }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        decoder_filters_[0]->callbacks_->addDecodedData(data);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        encoder_filters_[0]->callbacks_->addEncodedData(data);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyContinuation) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);
      }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));

  Buffer::OwnedImpl data("hello");
  decoder_filters_[0]->callbacks_->addDecodedData(data);
  decoder_filters_[0]->callbacks_->continueDecoding();

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  Buffer::OwnedImpl data2("hello");
  encoder_filters_[0]->callbacks_->addEncodedData(data2);
  encoder_filters_[0]->callbacks_->continueEncoding();
}

TEST_F(HttpConnectionManagerImplTest, MultipleFilters) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), false);

        Buffer::OwnedImpl fake_data("hello");
        decoder->decodeData(fake_data, false);

        Buffer::OwnedImpl fake_data2("world");
        decoder->decodeData(fake_data2, true);
      }));

  setupFilterChain(3, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(), decoder_filters_[0]->callbacks_->ssl());
        return FilterHeadersStatus::StopIteration;
      }));

  // Test route caching.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _));

  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input);

  // Mimic a decoder filter that trapped data and now sends it on, since the data was buffered
  // by the first filter, we expect to get it in 1 decodeData() call.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(), decoder_filters_[1]->callbacks_->ssl());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  decoder_filters_[0]->callbacks_->continueDecoding();

  // Now start encoding and mimic trapping in the encoding filter.
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[0]->callbacks_->ssl());
  decoder_filters_[2]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[2]->callbacks_->encodeData(response_body, false);
  decoder_filters_[2]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
  EXPECT_EQ(ssl_connection_.get(), decoder_filters_[2]->callbacks_->ssl());

  // Now finish the encode.
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();
  encoder_filters_[0]->callbacks_->continueEncoding();

  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[1]->callbacks_->ssl());
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
}

} // Http
} // Envoy
