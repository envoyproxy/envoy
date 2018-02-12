#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/macros.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::Test;
using testing::_;

namespace Envoy {
namespace Http {

class HttpConnectionManagerImplTest : public Test, public ConnectionManagerConfig {
public:
  struct RouteConfigProvider : public Router::RouteConfigProvider {
    // Router::RouteConfigProvider
    Router::ConfigConstSharedPtr config() override { return route_config_; }
    const std::string versionInfo() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }

    std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  };

  HttpConnectionManagerImplTest()
      : access_log_path_("dummy_path"),
        access_logs_{AccessLog::InstanceSharedPtr{new AccessLog::FileAccessLog(
            access_log_path_, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
            log_manager_)}},
        codec_(new NiceMock<MockServerConnection>()),
        stats_{{ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_HISTOGRAM(fake_stats_))},
               "",
               fake_stats_},
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))},
        listener_stats_{CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_listener_stats_))} {

    // response_encoder_ is not a NiceMock on purpose. This prevents complaining about this
    // method only.
    EXPECT_CALL(response_encoder_, getStream()).Times(AtLeast(0));
  }

  ~HttpConnectionManagerImplTest() {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void setup(bool ssl, const std::string& server_name, bool tracing = true) {
    if (ssl) {
      ssl_connection_.reset(new Ssl::MockConnection());
    }

    server_name_ = server_name;
    ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_.get()));
    ON_CALL(Const(filter_callbacks_.connection_), ssl())
        .WillByDefault(Return(ssl_connection_.get()));
    filter_callbacks_.connection_.local_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
    filter_callbacks_.connection_.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    conn_manager_.reset(new ConnectionManagerImpl(*this, drain_close_, random_, tracer_, runtime_,
                                                  local_info_, cluster_manager_));
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);

    if (tracing) {
      tracing_config_.reset(new TracingConnectionManagerConfig(
          {Tracing::OperationName::Ingress, {LowerCaseString(":method")}}));
    }
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
        .WillOnce(Invoke([num_decoder_filters, num_encoder_filters,
                          this](FilterChainFactoryCallbacks& callbacks) -> void {
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

  void setUpBufferLimits() {
    ON_CALL(response_encoder_, getStream()).WillByDefault(ReturnRef(stream_));
    EXPECT_CALL(stream_, addCallbacks(_))
        .WillOnce(Invoke(
            [&](Http::StreamCallbacks& callbacks) -> void { stream_callbacks_ = &callbacks; }));
    EXPECT_CALL(stream_, bufferLimit()).WillOnce(Return(initial_buffer_limit_));
  }

  void setUpEncoderAndDecoder() {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
      StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
      HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
      decoder->decodeHeaders(std::move(headers), true);
    }));

    setupFilterChain(2, 2);

    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          Buffer::OwnedImpl data("hello");
          decoder_filters_[0]->callbacks_->addDecodedData(data, true);
          return FilterHeadersStatus::Continue;
        }));
  }

  void sendReqestHeadersAndData() {
    EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::StopIteration));
    auto status = streaming_filter_ ? FilterDataStatus::StopIterationAndWatermark
                                    : FilterDataStatus::StopIterationAndBuffer;
    EXPECT_CALL(*decoder_filters_[1], decodeData(_, true)).WillOnce(Return(status));

    // Kick off the incoming data. |fake_input| is not sent, but instead kicks
    // off sending the headers and |data| queued up in setUpEncoderAndDecoder().
    Buffer::OwnedImpl fake_input("asdf");
    conn_manager_->onData(fake_input, false);
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

  void expectOnUpstreamInitFailure() {
    StreamDecoder* decoder = nullptr;
    NiceMock<MockStreamEncoder> encoder;

    ON_CALL(route_config_provider_.route_config_->route_->route_entry_, useWebSocket())
        .WillByDefault(Return(true));

    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
      decoder = &conn_manager_->newStream(encoder);
      HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {"connection", "Upgrade"},
                                                 {"upgrade", "websocket"}}};
      decoder->decodeHeaders(std::move(headers), true);
      data.drain(4);
    }));

    EXPECT_CALL(encoder, encodeHeaders(_, true))
        .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
          EXPECT_STREQ("503", headers.Status()->value().c_str());
        }));

    Buffer::OwnedImpl fake_input("1234");
    conn_manager_->onData(fake_input, false);
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
  uint32_t xffNumTrustedHops() const override { return 0; }
  Http::ForwardClientCertType forwardClientCert() override { return forward_client_cert_; }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Network::Address::Instance& localAddress() override { return local_address_; }
  const Optional<std::string>& userAgent() override { return user_agent_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }
  ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return false; }

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
  bool use_remote_address_{true};
  Http::ForwardClientCertType forward_client_cert_{Http::ForwardClientCertType::Sanitize};
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::unique_ptr<Ssl::MockConnection> ssl_connection_;
  RouteConfigProvider route_config_provider_;
  TracingConnectionManagerConfigPtr tracing_config_;
  SlowDateProviderImpl date_provider_;
  MockStream stream_;
  Http::StreamCallbacks* stream_callbacks_{nullptr};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  uint32_t initial_buffer_limit_{};
  bool streaming_filter_{false};
  Stats::IsolatedStoreImpl fake_listener_stats_;
  ConnectionManagerListenerStats listener_stats_;

  // TODO(mattklein123): Not all tests have been converted over to better setup. Convert the rest.
  MockStreamEncoder response_encoder_;
  std::vector<MockStreamDecoderFilter*> decoder_filters_;
  std::vector<MockStreamEncoderFilter*> encoder_filters_;
};

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(false, "envoy-custom-server", false);

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
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

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
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponse) {
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_STREQ("http", headers.ForwardedProto()->value().c_str());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(1)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
        decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);

        HeaderMapPtr continue_headers{new TestHeaderMapImpl{{":status", "100"}}};
        filter->callbacks_->encode100ContinueHeaders(std::move(continue_headers));
        HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithEncoderFilters) {
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));
  HeaderMapPtr continue_headers{new TestHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);
}

TEST_F(HttpConnectionManagerImplTest, PauseResume100Continue) {
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Stop the 100-Continue at filter 0. Filter 1 should not yet receive the 100-Continue
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_)).Times(0);
  HeaderMapPtr continue_headers{new TestHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  // Have the filter 0 continue. Make sure the 100-Continue is resumed as expected.
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));
  encoder_filters_[0]->callbacks_->continueEncoding();

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "http://api.lyft.com/"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  MockStreamFilter* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("404", headers.Status()->value().c_str());
      }));
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const RequestInfo::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

        return span;
      }));
  // No decorator.
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator())
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  // Verify tag is set based on the request headers.
  EXPECT_CALL(*span, setTag(":method", "GET"));
  // Verify if the activeSpan interface returns reference to the current span.
  EXPECT_CALL(*span, setTag("service-cluster", "scoobydoo"));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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

  // Should be no 'x-envoy-decorator-operation' response header.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1UL, tracing_stats_.service_forced_.value());
  EXPECT_EQ(0UL, tracing_stats_.random_sampling_.value());
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecorator) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const RequestInfo::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

        return span;
      }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(
          Invoke([&](const Tracing::Span& applyToSpan) -> void { EXPECT_EQ(span, &applyToSpan); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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

  // Verify decorator operation response header has been defined.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("testOp", headers.EnvoyDecoratorOperation()->value().c_str());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorOverrideOp) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const RequestInfo::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

        return span;
      }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(
          Invoke([&](const Tracing::Span& applyToSpan) -> void { EXPECT_EQ(span, &applyToSpan); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation("testOp"));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":method", "GET"},
                              {":authority", "host"},
                              {":path", "/"},
                              {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"},
                              {"x-envoy-decorator-operation", "testOp"}}};
    decoder->decodeHeaders(std::move(headers), true);

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), true);
    filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

    data.drain(4);
  }));

  // Should be no 'x-envoy-decorator-operation' response header, as decorator
  // was overridden by request header.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecorator) {
  setup(false, "");
  tracing_config_.reset(new TracingConnectionManagerConfig(
      {Tracing::OperationName::Egress, {LowerCaseString(":method")}}));

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const RequestInfo::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

        return span;
      }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(
          Invoke([&](const Tracing::Span& applyToSpan) -> void { EXPECT_EQ(span, &applyToSpan); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.EnvoyDecoratorOperation());
        // Verify that decorator operation has been set as request header.
        EXPECT_STREQ("testOp", headers.EnvoyDecoratorOperation()->value().c_str());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorOverrideOp) {
  setup(false, "");
  tracing_config_.reset(new TracingConnectionManagerConfig(
      {Tracing::OperationName::Egress, {LowerCaseString(":method")}}));

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _))
      .WillOnce(Invoke([&](const Tracing::Config& config, const HeaderMap&,
                           const RequestInfo::RequestInfo&) -> Tracing::Span* {
        EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

        return span;
      }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(
          Invoke([&](const Tracing::Span& applyToSpan) -> void { EXPECT_EQ(span, &applyToSpan); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  // Verify that span operation overridden by value supplied in response header.
  EXPECT_CALL(*span, setOperation("testOp"));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":method", "GET"},
                              {":authority", "host"},
                              {":path", "/"},
                              {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
    decoder->decodeHeaders(std::move(headers), true);

    HeaderMapPtr response_headers{
        new TestHeaderMapImpl{{":status", "200"}, {"x-envoy-decorator-operation", "testOp"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), true);
    filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
          [](const HeaderMap*, const HeaderMap*, const RequestInfo::RequestInfo& request_info) {
            EXPECT_TRUE(request_info.responseCode().valid());
            EXPECT_EQ(request_info.responseCode().value(), uint32_t(200));
            EXPECT_NE(nullptr, request_info.downstreamLocalAddress());
            EXPECT_NE(nullptr, request_info.downstreamRemoteAddress());
            EXPECT_NE(nullptr, request_info.routeEntry());
          }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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
  conn_manager_->onData(fake_input, false);
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
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartSpanOnlyHealthCheckRequest) {
  setup(false, "");

  NiceMock<Tracing::MockSpan>* span = new NiceMock<Tracing::MockSpan>();

  EXPECT_CALL(tracer_, startSpan_(_, _, _)).WillOnce(Return(span));
  EXPECT_CALL(*span, finishSpan()).Times(0);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](HeaderMap&, bool) -> FilterHeadersStatus {
        filter->callbacks_->requestInfo().healthCheck(true);
        return FilterHeadersStatus::StopIteration;
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
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
  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("404", headers.Status()->value().c_str());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, RejectWebSocketOnNonWebSocketRoute) {
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                               {":method", "GET"},
                                               {":path", "/"},
                                               {"connection", "Upgrade"},
                                               {"upgrade", "websocket"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("403", headers.Status()->value().c_str());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_ws_on_non_ws_route_.value());
}

TEST_F(HttpConnectionManagerImplTest, AllowNonWebSocketOnWebSocketRoute) {
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;

  // Websocket enabled route
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, useWebSocket())
      .WillByDefault(Return(true));

  // Non websocket request
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_cx_websocket_active_.value());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_websocket_total_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http1_active_.value());
}

TEST_F(HttpConnectionManagerImplTest, WebSocketNoThreadLocalCluster) {
  setup(false, "");

  EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(nullptr));
  expectOnUpstreamInitFailure();
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_active_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_total_.value());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http1_active_.value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
  EXPECT_EQ(0U, stats_.named_.downstream_cx_websocket_active_.value());
}

TEST_F(HttpConnectionManagerImplTest, WebSocketNoConnInPool) {
  setup(false, "");

  Upstream::MockHost::MockCreateConnectionData conn_info;
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_(_, _)).WillOnce(Return(conn_info));

  expectOnUpstreamInitFailure();
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_active_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_total_.value());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http1_active_.value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
  EXPECT_EQ(0U, stats_.named_.downstream_cx_websocket_active_.value());
}

TEST_F(HttpConnectionManagerImplTest, WebSocketConnectTimeoutError) {
  setup(false, "");

  Event::MockTimer* connect_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
  NiceMock<Network::MockClientConnection>* upstream_connection =
      new NiceMock<Network::MockClientConnection>();
  Upstream::MockHost::MockCreateConnectionData conn_info;

  conn_info.connection_ = upstream_connection;
  conn_info.host_description_.reset(
      new Upstream::HostImpl(cluster_manager_.thread_local_cluster_.cluster_.info_, "newhost",
                             Network::Utility::resolveUrl("tcp://127.0.0.1:80"),
                             envoy::api::v2::core::Metadata::default_instance(), 1,
                             envoy::api::v2::core::Locality().default_instance()));
  EXPECT_CALL(*connect_timer, enableTimer(_));
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _)).WillOnce(Return(conn_info));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;

  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, useWebSocket())
      .WillByDefault(Return(true));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                               {":method", "GET"},
                                               {":path", "/"},
                                               {"connection", "Upgrade"},
                                               {"upgrade", "websocket"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("504", headers.Status()->value().c_str());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  connect_timer->callback_();
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
}

TEST_F(HttpConnectionManagerImplTest, WebSocketConnectionFailure) {
  setup(false, "");

  Event::MockTimer* connect_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
  NiceMock<Network::MockClientConnection>* upstream_connection =
      new NiceMock<Network::MockClientConnection>();
  Upstream::MockHost::MockCreateConnectionData conn_info;

  conn_info.connection_ = upstream_connection;
  conn_info.host_description_.reset(
      new Upstream::HostImpl(cluster_manager_.thread_local_cluster_.cluster_.info_, "newhost",
                             Network::Utility::resolveUrl("tcp://127.0.0.1:80"),
                             envoy::api::v2::core::Metadata::default_instance(), 1,
                             envoy::api::v2::core::Locality().default_instance()));
  EXPECT_CALL(*connect_timer, enableTimer(_));
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _)).WillOnce(Return(conn_info));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;

  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, useWebSocket())
      .WillByDefault(Return(true));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                               {":method", "GET"},
                                               {":path", "/"},
                                               {"connection", "Upgrade"},
                                               {"upgrade", "websocket"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_STREQ("504", headers.Status()->value().c_str());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // expectOnUpstreamInitFailure("504");
  upstream_connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
}

TEST_F(HttpConnectionManagerImplTest, WebSocketPrefixAndAutoHostRewrite) {
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  NiceMock<Network::MockClientConnection>* upstream_connection =
      new NiceMock<Network::MockClientConnection>();
  Upstream::MockHost::MockCreateConnectionData conn_info;
  HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                             {":method", "GET"},
                                             {":path", "/"},
                                             {"connection", "Upgrade"},
                                             {"upgrade", "websocket"}}};
  auto raw_header_ptr = headers.get();

  conn_info.connection_ = upstream_connection;
  conn_info.host_description_.reset(
      new Upstream::HostImpl(cluster_manager_.thread_local_cluster_.cluster_.info_, "newhost",
                             Network::Utility::resolveUrl("tcp://127.0.0.1:80"),
                             envoy::api::v2::core::Metadata::default_instance(), 1,
                             envoy::api::v2::core::Locality().default_instance()));
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _)).WillOnce(Return(conn_info));

  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, useWebSocket())
      .WillByDefault(Return(true));

  EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_,
              finalizeRequestHeaders(_, _));
  EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, autoHostRewrite())
      .WillOnce(Return(true));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  upstream_connection->raiseEvent(Network::ConnectionEvent::Connected);

  // rewritten authority header when auto_host_rewrite is true
  EXPECT_STREQ("newhost", raw_header_ptr->Host()->value().c_str());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_active_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_websocket_total_.value());
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http1_active_.value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
  EXPECT_EQ(0U, stats_.named_.downstream_cx_websocket_active_.value());
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);
  EXPECT_EQ(ssl_connection_.get(), filter->callbacks_->connection()->ssl());

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->callback_();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_3xx_.value());
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  InSequence s;
  setup(false, "envoy-server-test");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder->decodeData(data, true);
  }));

  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    conn_manager_->newStream(encoder);
    data.drain(2);
  }));

  setupFilterChain(1, 0);

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Now raise a remote disconnection, we should see the filter get reset called.
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamProtocolError) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    conn_manager_->newStream(response_encoder_);
    throw CodecProtocolException("protocol error");
  }));

  setupFilterChain(1, 0);

  // A protocol exception should result in reset of the streams followed by a local close.
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
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
  conn_manager_->onData(fake_input, false);

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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
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
  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
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
  conn_manager_->onData(fake_input, false);

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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
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
        decoder_filters_[0]->callbacks_->addDecodedData(trailers_data, true);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(Ref(trailers_data), false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeTrailers(_))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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
        encoder_filters_[0]->callbacks_->addEncodedData(trailers_data, true);
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

// Add*Data during the *Data callbacks.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyDuringDecodeData) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl data1("hello");
    decoder->decodeData(data1, false);

    Buffer::OwnedImpl data2("world");
    decoder->decodeData(data2, true);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        decoder_filters_[0]->callbacks_->addDecodedData(data, true);
        EXPECT_EQ(TestUtility::bufferToString(*decoder_filters_[0]->callbacks_->decodingBuffer()),
                  "helloworld");
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        encoder_filters_[0]->callbacks_->addEncodedData(data, true);
        EXPECT_EQ(TestUtility::bufferToString(*encoder_filters_[0]->callbacks_->encodingBuffer()),
                  "goodbye");
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl data1("good");
  decoder_filters_[1]->callbacks_->encodeData(data1, false);
  Buffer::OwnedImpl data2("bye");
  decoder_filters_[1]->callbacks_->encodeData(data2, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInline) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        decoder_filters_[0]->callbacks_->addDecodedData(data, true);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        encoder_filters_[0]->callbacks_->addEncodedData(data, true);
        EXPECT_EQ(5UL, encoder_filters_[0]->callbacks_->encodingBuffer()->length());
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

TEST_F(HttpConnectionManagerImplTest, FilterClearRouteCache) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  setupFilterChain(3, 2);

  Router::RouteConstSharedPtr route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  Router::RouteConstSharedPtr route2 = std::make_shared<NiceMock<Router::MockRoute>>();

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _))
      .WillOnce(Return(route1))
      .WillOnce(Return(route2))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(route1->routeEntry(),
                  decoder_filters_[0]->callbacks_->requestInfo().routeEntry());
        decoder_filters_[0]->callbacks_->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(route2->routeEntry(),
                  decoder_filters_[1]->callbacks_->requestInfo().routeEntry());
        decoder_filters_[1]->callbacks_->clearRouteCache();
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->route());
        EXPECT_EQ(nullptr, decoder_filters_[2]->callbacks_->requestInfo().routeEntry());
        return FilterHeadersStatus::StopIteration;
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, UpstreamWatermarkCallbacks) {
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Mimic the upstream connection backing up. The router would call
  // onDecoderFilterAboveWriteBufferHighWatermark which should readDisable the stream and increment
  // stats.
  EXPECT_CALL(response_encoder_, getStream()).Times(1).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(true));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_paused_reading_total_.value());

  // Resume the flow of data. When the router buffer drains it calls
  // onDecoderFilterBelowWriteBufferLowWatermark which should re-enable reads on the stream.
  EXPECT_CALL(response_encoder_, getStream()).Times(1).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(false));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, stats_.named_.downstream_flow_control_resumed_reading_total_.value());

  // Backup upstream once again.
  EXPECT_CALL(response_encoder_, getStream()).Times(1).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(true));
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  decoder_filters_[0]->callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(2U, stats_.named_.downstream_flow_control_paused_reading_total_.value());

  // Send a full response.
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  // When the stream ends, the manager should check to see if the connection is
  // read disabled, and keep calling readDisable(false) until readEnabled()
  // returns true.
  EXPECT_CALL(filter_callbacks_.connection_, readEnabled())
      .Times(2)
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
  expectOnDestroy();
  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);
}

TEST_F(HttpConnectionManagerImplTest, DownstreamWatermarkCallbacks) {
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Test what happens when there are no subscribers.
  conn_manager_->onAboveWriteBufferHighWatermark();
  conn_manager_->onBelowWriteBufferLowWatermark();

  // The connection manger will outlive callbacks but never reference them once deleted.
  MockDownstreamWatermarkCallbacks callbacks;

  // Network::Connection callbacks are passed through the codec
  ASSERT(decoder_filters_[0]->callbacks_ != nullptr);
  EXPECT_CALL(*codec_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn_manager_->onAboveWriteBufferHighWatermark();
  EXPECT_CALL(*codec_, onUnderlyingConnectionBelowWriteBufferLowWatermark());
  conn_manager_->onBelowWriteBufferLowWatermark();

  // Now add a watermark subscriber and make sure both the high and low watermark callbacks are
  // propogated.
  ASSERT_NE(0, decoder_filters_.size());
  decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);
  // Make sure encoder filter callbacks are propogated to the watermark subscriber.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  encoder_filters_[0]->callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  encoder_filters_[0]->callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();

  ASSERT(stream_callbacks_ != nullptr);
  // Finally make sure that watermark events on the downstream stream are passed to the watermark
  // subscriber.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  stream_callbacks_->onAboveWriteBufferHighWatermark();
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  stream_callbacks_->onBelowWriteBufferLowWatermark();

  // Set things up so the callbacks have been called twice.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  stream_callbacks_->onAboveWriteBufferHighWatermark();
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  encoder_filters_[0]->callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();

  // Now unsubscribe and verify no further callbacks are called.
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  decoder_filters_[0]->callbacks_->removeDownstreamWatermarkCallbacks(callbacks);
}

TEST_F(HttpConnectionManagerImplTest, UnderlyingConnectionWatermarksPassedOn) {
  setup(false, "");

  // Make sure codec_ is created.
  EXPECT_CALL(*codec_, dispatch(_));
  Buffer::OwnedImpl fake_input("");
  conn_manager_->onData(fake_input, false);

  // Mark the connection manger as backed up before the stream is created.
  ASSERT_EQ(decoder_filters_.size(), 0);
  EXPECT_CALL(*codec_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  conn_manager_->onAboveWriteBufferHighWatermark();

  // Now when the stream is created, it should be informed of the connection
  // callbacks immediately.
  EXPECT_CALL(filter_callbacks_.connection_, aboveHighWatermark()).WillOnce(Return(true));
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();
  ASSERT_GE(decoder_filters_.size(), 1);
  MockDownstreamWatermarkCallbacks callbacks;
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);
}

TEST_F(HttpConnectionManagerImplTest, AlterFilterWatermarkLimits) {
  initial_buffer_limit_ = 100;
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

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
}

TEST_F(HttpConnectionManagerImplTest, HitFilterWatermarkLimits) {
  initial_buffer_limit_ = 1;
  streaming_filter_ = true;
  setup(false, "");
  setUpEncoderAndDecoder();

  // The filter is a streaming filter. Sending 4 bytes should hit the
  // watermark limit and disable reads on the stream.
  EXPECT_CALL(stream_, readDisable(true));
  sendReqestHeadersAndData();

  // Change the limit so the buffered data is below the new watermark. The
  // stream should be read-enabled
  EXPECT_CALL(stream_, readDisable(false));
  int buffer_len = decoder_filters_[0]->callbacks_->decodingBuffer()->length();
  decoder_filters_[0]->callbacks_->setDecoderBufferLimit((buffer_len + 1) * 2);

  // Start the response
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  MockDownstreamWatermarkCallbacks callbacks;
  decoder_filters_[0]->callbacks_->addDownstreamWatermarkCallbacks(callbacks);

  // Now overload the buffer with response data. The downstream watermark
  // callbacks should be called.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl fake_response("A long enough string to go over watermarks");
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);

  // Change the limit so the buffered data is below the new watermark.
  buffer_len = encoder_filters_[0]->callbacks_->encodingBuffer()->length();
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  encoder_filters_[0]->callbacks_->setEncoderBufferLimit((buffer_len + 1) * 2);
}

TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimits) {
  initial_buffer_limit_ = 10;
  streaming_filter_ = false;
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Set the filter to be a buffering filter. Sending any data will hit the
  // watermark limit and result in a 413 being sent to the user.
  Http::TestHeaderMapImpl response_headers{
      {":status", "413"}, {"content-length", "17"}, {"content-type", "text/plain"}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));
  Buffer::OwnedImpl data("A longer string");
  decoder_filters_[0]->callbacks_->addDecodedData(data, false);
}

// Return 413 from an intermediate filter and make sure we don't continue the filter chain.
TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimitsIntermediateFilter) {
  InSequence s;
  initial_buffer_limit_ = 10;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    Buffer::OwnedImpl fake_data2("world world");
    decoder->decodeData(fake_data2, true);
  }));

  setUpBufferLimits();
  setupFilterChain(2, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));
  Http::TestHeaderMapImpl response_headers{
      {":status", "413"}, {"content-length", "17"}, {"content-type", "text/plain"}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndWatermark));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsBeforeHeaders) {
  initial_buffer_limit_ = 10;
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Start the response without processing the request headers through all
  // filters.
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early response.

  expectOnDestroy();
  Http::TestHeaderMapImpl expected_response_headers{
      {":status", "500"}, {"content-length", "21"}, {"content-type", "text/plain"}};
  Buffer::OwnedImpl fake_response("A long enough string to go over watermarks");
  // Fake response starts doing through the filter.
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  std::string response_body;
  // The 500 goes directly to the encoder.
  EXPECT_CALL(response_encoder_,
              encodeHeaders(HeaderMapEqualRef(&expected_response_headers), false));
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);
  EXPECT_EQ("Internal Server Error", response_body);

  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsAfterHeaders) {
  initial_buffer_limit_ = 10;
  setup(false, "");
  setUpEncoderAndDecoder();
  sendReqestHeadersAndData();

  // Start the response, and make sure the request headers are fully processed.
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early reset.
  const std::string data = "A long enough string to go over watermarks";
  Buffer::OwnedImpl fake_response(data);
  InSequence s;
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(stream_, resetStream(_));
  decoder_filters_[0]->callbacks_->encodeData(fake_response, false);

  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyContinuation) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::Continue));

  Buffer::OwnedImpl data("hello");
  decoder_filters_[0]->callbacks_->addDecodedData(data, true);
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
  encoder_filters_[0]->callbacks_->addEncodedData(data2, true);
  encoder_filters_[0]->callbacks_->continueEncoding();
}

TEST_F(HttpConnectionManagerImplTest, MultipleFilters) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    Buffer::OwnedImpl fake_data2("world");
    decoder->decodeData(fake_data2, true);
  }));

  setupFilterChain(3, 2);

  // Test route caching.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(), decoder_filters_[0]->callbacks_->connection()->ssl());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*decoder_filters_[0], decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Mimic a decoder filter that trapped data and now sends it on, since the data was buffered
  // by the first filter, we expect to get it in 1 decodeData() call.
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(route_config_provider_.route_config_->route_,
                  decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(ssl_connection_.get(), decoder_filters_[1]->callbacks_->connection()->ssl());
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
  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[0]->callbacks_->connection()->ssl());
  decoder_filters_[2]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[2]->callbacks_->encodeData(response_body, false);
  decoder_filters_[2]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
  EXPECT_EQ(ssl_connection_.get(), decoder_filters_[2]->callbacks_->connection()->ssl());

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

  EXPECT_EQ(ssl_connection_.get(), encoder_filters_[1]->callbacks_->connection()->ssl());
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

} // namespace Http
} // namespace Envoy
