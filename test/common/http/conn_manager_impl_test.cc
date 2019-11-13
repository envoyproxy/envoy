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
#include "common/common/empty_string.h"
#include "common/common/macros.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/context_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/access_loggers/file/file_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::AnyNumber;
using testing::AtLeast;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Matcher;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

class HttpConnectionManagerImplTest : public testing::Test, public ConnectionManagerConfig {
public:
  HttpConnectionManagerImplTest()
      : http_context_(fake_stats_.symbolTable()), access_log_path_("dummy_path"),
        access_logs_{
            AccessLog::InstanceSharedPtr{new Extensions::AccessLoggers::File::FileAccessLog(
                access_log_path_, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
                log_manager_)}},
        codec_(new NiceMock<MockServerConnection>()),
        stats_{{ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_HISTOGRAM(fake_stats_))},
               "",
               fake_stats_},
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))},
        listener_stats_{CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_listener_stats_))} {

    http_context_.setTracer(tracer_);

    ON_CALL(route_config_provider_, lastUpdated())
        .WillByDefault(Return(test_time_.timeSystem().systemTime()));
    ON_CALL(scoped_route_config_provider_, lastUpdated())
        .WillByDefault(Return(test_time_.timeSystem().systemTime()));
    // response_encoder_ is not a NiceMock on purpose. This prevents complaining about this
    // method only.
    EXPECT_CALL(response_encoder_, getStream()).Times(AtLeast(0));
  }

  ~HttpConnectionManagerImplTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void setup(bool ssl, const std::string& server_name, bool tracing = true, bool use_srds = false) {
    use_srds_ = use_srds;
    if (ssl) {
      ssl_connection_ = std::make_shared<Ssl::MockConnectionInfo>();
    }

    server_name_ = server_name;
    ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_));
    ON_CALL(Const(filter_callbacks_.connection_), ssl()).WillByDefault(Return(ssl_connection_));
    filter_callbacks_.connection_.local_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
    filter_callbacks_.connection_.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    conn_manager_ = std::make_unique<ConnectionManagerImpl>(
        *this, drain_close_, random_, http_context_, runtime_, local_info_, cluster_manager_,
        &overload_manager_, test_time_.timeSystem());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);

    if (tracing) {
      envoy::type::FractionalPercent percent1;
      percent1.set_numerator(100);
      envoy::type::FractionalPercent percent2;
      percent2.set_numerator(10000);
      percent2.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);
      tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
          TracingConnectionManagerConfig{Tracing::OperationName::Ingress,
                                         {LowerCaseString(":method")},
                                         percent1,
                                         percent2,
                                         percent1,
                                         false,
                                         256});
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

  // If request_with_data_and_trailers is true, includes data and trailers in the request. If
  // decode_headers_stop_all is true, decoder_filters_[0]'s callback decodeHeaders() returns
  // StopAllIterationAndBuffer.
  void setUpEncoderAndDecoder(bool request_with_data_and_trailers, bool decode_headers_stop_all) {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_))
        .WillOnce(Invoke([&, request_with_data_and_trailers](Buffer::Instance&) -> void {
          StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
          HeaderMapPtr headers{
              new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
          if (request_with_data_and_trailers) {
            decoder->decodeHeaders(std::move(headers), false);

            Buffer::OwnedImpl fake_data("12345");
            decoder->decodeData(fake_data, false);

            HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
            decoder->decodeTrailers(std::move(trailers));
          } else {
            decoder->decodeHeaders(std::move(headers), true);
          }
        }));

    setupFilterChain(2, 2);

    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, _))
        .WillOnce(InvokeWithoutArgs([&, decode_headers_stop_all]() -> FilterHeadersStatus {
          Buffer::OwnedImpl data("hello");
          decoder_filters_[0]->callbacks_->addDecodedData(data, true);
          if (decode_headers_stop_all) {
            return FilterHeadersStatus::StopAllIterationAndBuffer;
          } else {
            return FilterHeadersStatus::Continue;
          }
        }));
    EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  }

  Event::MockTimer* setUpTimer() {
    // this timer belongs to whatever by whatever next creates a timer.
    // See Envoy::Event::MockTimer for details.
    return new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  }

  void sendRequestHeadersAndData() {
    EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::StopIteration));
    auto status = streaming_filter_ ? FilterDataStatus::StopIterationAndWatermark
                                    : FilterDataStatus::StopIterationAndBuffer;
    EXPECT_CALL(*decoder_filters_[1], decodeData(_, true)).WillOnce(Return(status));
    EXPECT_CALL(*decoder_filters_[1], decodeComplete());

    // Kick off the incoming data. |fake_input| is not sent, but instead kicks
    // off sending the headers and |data| queued up in setUpEncoderAndDecoder().
    Buffer::OwnedImpl fake_input("asdf");
    conn_manager_->onData(fake_input, false);
  }

  HeaderMap* sendResponseHeaders(HeaderMapPtr&& response_headers) {
    HeaderMap* altered_response_headers = nullptr;

    EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, _))
        .WillOnce(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
          altered_response_headers = &headers;
          return FilterHeadersStatus::Continue;
        }));
    EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
    decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);
    return altered_response_headers;
  }

  void expectOnDestroy() {
    for (auto filter : decoder_filters_) {
      EXPECT_CALL(*filter, onDestroy());
    }

    auto setup_filter_expect = [](MockStreamEncoderFilter* filter) {
      EXPECT_CALL(*filter, onDestroy());
    };
    std::for_each(encoder_filters_.rbegin(), encoder_filters_.rend(), setup_filter_expect);

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
  bool preserveExternalRequestId() const override { return false; }
  uint32_t maxRequestHeadersKb() const override { return max_request_headers_kb_; }
  uint32_t maxRequestHeadersCount() const override { return max_request_headers_count_; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  bool isRoutable() const override { return true; }
  absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override {
    return max_connection_duration_;
  }
  std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
  std::chrono::milliseconds requestTimeout() const override { return request_timeout_; }
  std::chrono::milliseconds delayedCloseTimeout() const override { return delayed_close_timeout_; }
  bool use_srds_{};
  Router::RouteConfigProvider* routeConfigProvider() override {
    if (use_srds_) {
      return nullptr;
    }
    return &route_config_provider_;
  }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    if (use_srds_) {
      return &scoped_route_config_provider_;
    }
    return nullptr;
  }
  const std::string& serverName() override { return server_name_; }
  HttpConnectionManagerProto::ServerHeaderTransformation serverHeaderTransformation() override {
    return server_transformation_;
  }
  ConnectionManagerStats& stats() override { return stats_; }
  ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return 0; }
  bool skipXffAppend() const override { return false; }
  const std::string& via() const override { return EMPTY_STRING; }
  Http::ForwardClientCertType forwardClientCert() override { return forward_client_cert_; }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Network::Address::Instance& localAddress() override { return local_address_; }
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }
  ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return normalize_path_; }
  bool shouldMergeSlashes() const override { return merge_slashes_; }

  DangerousDeprecatedTestTime test_time_;
  NiceMock<Router::MockRouteConfigProvider> route_config_provider_;
  std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  NiceMock<Router::MockScopedRouteConfigProvider> scoped_route_config_provider_;
  NiceMock<Tracing::MockHttpTracer> tracer_;
  Stats::IsolatedStoreImpl fake_stats_;
  Http::ContextImpl http_context_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  std::string access_log_path_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  MockServerConnection* codec_;
  NiceMock<MockFilterChainFactory> filter_factory_;
  ConnectionManagerStats stats_;
  ConnectionManagerTracingStats tracing_stats_;
  NiceMock<Network::MockDrainDecision> drain_close_;
  std::unique_ptr<ConnectionManagerImpl> conn_manager_;
  std::string server_name_;
  HttpConnectionManagerProto::ServerHeaderTransformation server_transformation_{
      HttpConnectionManagerProto::OVERWRITE};
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  bool use_remote_address_{true};
  Http::DefaultInternalAddressConfig internal_address_config_;
  Http::ForwardClientCertType forward_client_cert_{Http::ForwardClientCertType::Sanitize};
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  absl::optional<std::string> user_agent_;
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  std::chrono::milliseconds stream_idle_timeout_{};
  std::chrono::milliseconds request_timeout_{};
  std::chrono::milliseconds delayed_close_timeout_{};
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl_connection_;
  TracingConnectionManagerConfigPtr tracing_config_;
  SlowDateProviderImpl date_provider_{test_time_.timeSystem()};
  MockStream stream_;
  Http::StreamCallbacks* stream_callbacks_{nullptr};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  uint32_t initial_buffer_limit_{};
  bool streaming_filter_{false};
  Stats::IsolatedStoreImpl fake_listener_stats_;
  ConnectionManagerListenerStats listener_stats_;
  bool proxy_100_continue_ = false;
  bool preserve_external_request_id_ = false;
  Http::Http1Settings http1_settings_;
  bool normalize_path_ = false;
  bool merge_slashes_ = false;
  NiceMock<Network::MockClientConnection> upstream_conn_; // for websocket tests
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_; // for websocket tests

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
        EXPECT_EQ("http", headers.ForwardedProto()->value().getStringView());
        if (headers.Path()->value() == "/healthcheck") {
          filter->callbacks_->streamInfo().healthCheck(true);
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
          HeaderMapPtr headers{
              new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
          decoder->decodeHeaders(std::move(headers), true);
        } else {
          HeaderMapPtr headers{new TestHeaderMapImpl{
              {":authority", "host"}, {":path", "/healthcheck"}, {":method", "GET"}}};
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
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponse) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.ForwardedProto()->value().getStringView());
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
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    // Test not charging stats on the second call.
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
  EXPECT_EQ(2U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(2U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithEncoderFiltersProxyingDisabled) {
  proxy_100_continue_ = false;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Akin to 100ContinueResponseWithEncoderFilters below, but with
  // proxy_100_continue_ false. Verify the filters do not get the 100 continue
  // headers.
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_)).Times(0);
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

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithEncoderFilters) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

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
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Stop the 100-Continue at encoder filter 1. Encoder filter 0 should not yet receive the
  // 100-Continue
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_)).Times(0);
  HeaderMapPtr continue_headers{new TestHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[1]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  // Have the encoder filter 1 continue. Make sure the 100-Continue is resumed as expected.
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));
  encoder_filters_[1]->callbacks_->continueEncoding();

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[1]->callbacks_->encodeHeaders(std::move(response_headers), false);
}

// By default, Envoy will set the server header to the server name, here "custom-value"
TEST_F(HttpConnectionManagerImplTest, ServerHeaderOverwritten) {
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const HeaderMap* altered_headers = sendResponseHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("custom-value", altered_headers->Server()->value().getStringView());
}

// When configured APPEND_IF_ABSENT if the server header is present it will be retained.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendPresent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const HeaderMap* altered_headers = sendResponseHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->Server()->value().getStringView());
}

// When configured APPEND_IF_ABSENT if the server header is absent the server name will be set.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendAbsent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const HeaderMap* altered_headers =
      sendResponseHeaders(HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}});
  EXPECT_EQ("custom-value", altered_headers->Server()->value().getStringView());
}

// When configured PASS_THROUGH, the server name will pass through.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughPresent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const HeaderMap* altered_headers = sendResponseHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->Server()->value().getStringView());
}

// When configured PASS_THROUGH, the server header will not be added if absent.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughAbsent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const HeaderMap* altered_headers =
      sendResponseHeaders(HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}});
  EXPECT_TRUE(altered_headers->Server() == nullptr);
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", "http://api.lyft.com/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("404", headers.Status()->value().getStringView());
        EXPECT_EQ("absolute_path_rejected",
                  filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
      }));
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Invalid paths are rejected with 400.
TEST_F(HttpConnectionManagerImplTest, PathFailedtoSanitize) {
  InSequence s;
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"},
                              {":path", "/ab%00c"}, // "%00" is not valid in path according to RFC
                              {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.Status()->value().getStringView());
        EXPECT_EQ("path_normalization_failed",
                  filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
      }));
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Filters observe normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseSantizedPath) {
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](HeaderMap& header_map, bool) -> FilterHeadersStatus {
        EXPECT_EQ(normalized_path, header_map.Path()->value().getStringView());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// The router observes normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, RouteShouldUseSantizedPath) {
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  const std::string fake_cluster_name = "fake_cluster";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  std::shared_ptr<Router::MockRoute> route = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _))
      .WillOnce(
          Invoke([&](const Http::HeaderMap& header_map, const StreamInfo::StreamInfo&, uint64_t) {
            EXPECT_EQ(normalized_path, header_map.Path()->value().getStringView());
            return route;
          }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks&) -> void {}));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  // No decorator.
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator())
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  // Verify tag is set based on the request headers.
  EXPECT_CALL(*span, setTag(Eq(":method"), Eq("GET")));
  // Verify if the activeSpan interface returns reference to the current span.
  EXPECT_CALL(*span, setTag(Eq("service-cluster"), Eq("scoobydoo")));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
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

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
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
        EXPECT_EQ("testOp", headers.EnvoyDecoratorOperation()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorOverrideOp) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(Eq("testOp")));

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
  envoy::type::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {LowerCaseString(":method")},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
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
        EXPECT_EQ("testOp", headers.EnvoyDecoratorOperation()->value().getStringView());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorOverrideOp) {
  setup(false, "");
  envoy::type::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {LowerCaseString(":method")},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(4);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  // Verify that span operation overridden by value supplied in response header.
  EXPECT_CALL(*span, setOperation(Eq("testOp")));

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

TEST_F(HttpConnectionManagerImplTest,
       StartAndFinishSpanNormalFlowEgressDecoratorOverrideOpNoActiveSpan) {
  setup(false, "");
  envoy::type::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {LowerCaseString(":method")},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                                 An<const envoy::type::FractionalPercent&>(), _))
      .WillOnce(Return(false));
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

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLog) {
  static constexpr char local_address[] = "0.0.0.0";
  static constexpr char xff_address[] = "1.2.3.4";

  // stream_info.downstreamRemoteAddress will infer the address from request
  // headers instead of the physical connection
  use_remote_address_ = false;
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
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.routeEntry());

        EXPECT_EQ(stream_info.downstreamRemoteAddress()->ip()->addressAsString(), xff_address);
        EXPECT_EQ(stream_info.downstreamDirectRemoteAddress()->ip()->addressAsString(),
                  local_address);
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":method", "GET"},
                              {":authority", "host"},
                              {":path", "/"},
                              {"x-forwarded-for", xff_address},
                              {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
    decoder->decodeHeaders(std::move(headers), true);

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), true);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamDisconnectAccessLog) {
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
        EXPECT_TRUE(
            stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamConnectionTermination));
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogWithTrailers) {
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
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.routeEntry());
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
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    HeaderMapPtr response_trailers{new TestHeaderMapImpl{{"x-trailer", "1"}}};
    filter->callbacks_->encodeTrailers(std::move(response_trailers));

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogWithInvalidRequest) {
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
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(400));
        EXPECT_EQ("missing_host_header", stream_info.responseCodeDetails().value());
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_EQ(nullptr, stream_info.routeEntry());
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    // These request headers are missing the necessary ":host"
    HeaderMapPtr headers{new TestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(0);
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogSsl) {
  setup(true, "");

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
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamSslConnection());
        EXPECT_NE(nullptr, stream_info.routeEntry());
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
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    HeaderMapPtr response_trailers{new TestHeaderMapImpl{{"x-trailer", "1"}}};
    filter->callbacks_->encodeTrailers(std::move(response_trailers));

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, DoNotStartSpanIfTracingIsNotEnabled) {
  setup(false, "");

  // Disable tracing.
  tracing_config_.reset();

  EXPECT_CALL(tracer_, startSpan_(_, _, _, _)).Times(0);
  ON_CALL(runtime_.snapshot_,
          featureEnabled("tracing.global_enabled", An<const envoy::type::FractionalPercent&>(), _))
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
        EXPECT_EQ("404", headers.Status()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// No idle timeout when route idle timeout is implied at both global and
// per-route level. The connection manager config is responsible for managing
// the default configuration aspects.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutNotConfigured) {
  setup(false, "");

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// When the global timeout is configured, the timer is enabled before we receive
// headers, if it fires we don't faceplant.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutGlobal) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.Status()->value().getStringView());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, AccessEncoderRouteBeforeHeadersArriveOnIdleTimeout) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  std::shared_ptr<MockStreamEncoderFilter> filter(new NiceMock<MockStreamEncoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamEncoderFilter(filter);
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    // Simulate and idle timeout so that the filter chain gets created.
    idle_timer->invokeCallback();
  }));

  // This should not be called as we don't have request headers.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _)).Times(0);

  EXPECT_CALL(*filter, encodeHeaders(_, _))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Under heavy load it is possible that stream timeout will be reached before any headers
        // were received. Envoy will create a local reply that will go through the encoder filter
        // chain. We want to make sure that encoder filters get a null route object.
        auto route = filter->callbacks_->route();
        EXPECT_EQ(route.get(), nullptr);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*filter, encodeData(_, _));
  EXPECT_CALL(*filter, encodeComplete());
  EXPECT_CALL(*filter, onDestroy());

  EXPECT_CALL(response_encoder_, encodeHeaders(_, _));
  EXPECT_CALL(response_encoder_, encodeData(_, _));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestStreamIdleAccessLog) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
  }));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.Status()->value().getStringView());
      }));

  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout));
      }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Per-route timeouts override the global stream idle timeout.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutRouteOverride) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(30)));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(30), _));
    decoder->decodeHeaders(std::move(headers), false);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Per-route zero timeout overrides the global stream idle timeout.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutRouteZeroOverride) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(0)));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, disableTimer());
    decoder->decodeHeaders(std::move(headers), false);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after having sent downstream headers.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.Status()->value().getStringView());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timer is properly disabled when the stream terminates normally.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutNormalTermination) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    data.drain(4);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*idle_timer, disableTimer());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after having sent downstream
// headers+body.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterDownstreamHeadersAndBody) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeData(data, false);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.Status()->value().getStringView());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after upstream headers have been sent.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterUpstreamHeaders) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Codec sends downstream request headers, upstream response headers are
  // encoded.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
  }));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.Status()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after a sequence of header/data events.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterBidiData) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));
  proxy_100_continue_ = true;

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Codec sends downstream request headers, upstream response headers are
  // encoded, data events happen in various directions.
  Event::MockTimer* idle_timer = setUpTimer();
  StreamDecoder* decoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    HeaderMapPtr response_continue_headers{new TestHeaderMapImpl{{":status", "100"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encode100ContinueHeaders(std::move(response_continue_headers));

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeData(data, false);

    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeTrailers(std::move(trailers));

    Buffer::OwnedImpl fake_response("world");
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeData(fake_response, false);

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
  }));

  // 100 continue.
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.Status()->value().getStringView());
      }));

  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, false)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
  EXPECT_EQ("world", response_body);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutDisabledByDefault) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutDisabledIfSetToZero) {
  request_timeout_ = std::chrono::milliseconds(0);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutValidlyConfigured) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));

    conn_manager_->newStream(response_encoder_);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutCallbackDisarmsAndReturns408) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  std::string response_body;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(AtLeast(1));

    EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
          EXPECT_EQ("408", headers.Status()->value().getStringView());
        }));
    EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

    conn_manager_->newStream(response_encoder_);
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_)).Times(2);
    request_timer->invokeCallback();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(1U, stats_.named_.downstream_rq_timeout_.value());
  EXPECT_EQ("request timeout", response_body);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsNotDisarmedOnIncompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(0);

    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    // the second parameter 'false' leaves the stream open
    decoder->decodeHeaders(std::move(headers), false);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    EXPECT_CALL(*request_timer, disableTimer()).Times(1);
    decoder->decodeHeaders(std::move(headers), true);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithData) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    decoder->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(1);
    decoder->decodeData(data, true);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithTrailers) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
    decoder->decodeData(data, false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(1);
    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnEncodeHeaders) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, _));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(1);
    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnConnectionTermination) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  Event::MockTimer* request_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder->decodeHeaders(std::move(headers), false);
  }));

  Buffer::OwnedImpl fake_input("1234");

  EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*request_timer, disableTimer()).Times(1);
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
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
        EXPECT_EQ("403", headers.Status()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_ws_on_non_ws_route_.value());
}

// Make sure for upgrades, we do not append Connection: Close when draining.
TEST_F(HttpConnectionManagerImplTest, FooUpgradeDrainClose) {
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillRepeatedly(Invoke([&](HeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, encodeHeaders(_, false))
      .WillRepeatedly(Invoke(
          [&](HeaderMap&, bool) -> FilterHeadersStatus { return FilterHeadersStatus::Continue; }));

  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("upgrade", headers.Connection()->value().getStringView());
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain(_, _, _))
      .WillRepeatedly(Invoke([&](absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                 FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
        return true;
      }));

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{new TestHeaderMapImpl{{":authority", "host"},
                                               {":method", "GET"},
                                               {":path", "/"},
                                               {"connection", "Upgrade"},
                                               {"upgrade", "foo"}}};
    decoder->decodeHeaders(std::move(headers), false);

    HeaderMapPtr response_headers{
        new TestHeaderMapImpl{{":status", "101"}, {"Connection", "upgrade"}, {"upgrade", "foo"}}};
    filter->decoder_callbacks_->encodeHeaders(std::move(response_headers), false);

    data.drain(4);
  }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
        EXPECT_EQ("https", headers.ForwardedProto()->value().getStringView());
        return FilterHeadersStatus::StopIteration;
      }));

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);
  EXPECT_EQ(ssl_connection_.get(), filter->callbacks_->connection()->ssl().get());

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, ResponseBeforeRequestComplete) {
  InSequence s;
  setup(false, "envoy-server-test");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
        EXPECT_EQ("envoy-server-test", headers.Server()->value().getStringView());
      }));
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true);
}

TEST_F(HttpConnectionManagerImplTest, DisconnectOnProxyConnectionDisconnect) {
  InSequence s;
  setup(false, "envoy-server-test");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", "/"}, {":method", "GET"}, {"proxy-connection", "close"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("close", headers.Connection()->value().getStringView());
        EXPECT_EQ(nullptr, headers.ProxyConnection());
      }));
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

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
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);

  // Start the response
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("", headers.Server()->value().getStringView());
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
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    conn_manager_->newStream(response_encoder_);
    throw CodecProtocolException("protocol error");
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

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    throw CodecProtocolException("protocol error");
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

  StreamDecoder* decoder = nullptr;
  NiceMock<MockStreamEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);

    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);

    throw CodecProtocolException("protocol error");
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Verify that FrameFloodException causes connection to be closed abortively.
TEST_F(HttpConnectionManagerImplTest, FrameFloodError) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    conn_manager_->newStream(response_encoder_);
    throw FrameFloodException("too many outbound frames.");
  }));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // FrameFloodException should result in reset of the streams followed by abortive close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_LOG_NOT_CONTAINS("warning", "downstream HTTP flood",
                          conn_manager_->onData(fake_input, false));
}

// Verify that FrameFloodException causes connection to be closed abortively as well as logged
// if runtime indicates to do so.
TEST_F(HttpConnectionManagerImplTest, FrameFloodErrorWithLog) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    conn_manager_->newStream(response_encoder_);
    throw FrameFloodException("too many outbound frames.");
  }));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("http.connection_manager.log_flood_exception",
                                                 Matcher<const envoy::type::FractionalPercent&>(_)))
      .WillOnce(Return(true));

  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_));
  EXPECT_CALL(filter_factory_, createFilterChain(_)).Times(0);

  // FrameFloodException should result in reset of the streams followed by abortive close.
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  EXPECT_LOG_CONTAINS("warning",
                      "downstream HTTP flood from IP '0.0.0.0:0': too many outbound frames.",
                      conn_manager_->onData(fake_input, false));
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

  NiceMock<MockStreamEncoder> encoder;
  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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

  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);

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

  NiceMock<MockStreamEncoder> encoder;
  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(encoder);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
  }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);

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
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
  }));

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

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
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
    decoder->decodeData(fake_data, true);
  }));

  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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
}

TEST_F(HttpConnectionManagerImplTest, ZeroByteDataFiltering) {
  InSequence s;
  setup(false, "");

  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  Buffer::OwnedImpl zero;
  decoder->decodeData(zero, true);

  // Continue.
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  decoder_filters_[0]->callbacks_->continueDecoding();
}

TEST_F(HttpConnectionManagerImplTest, FilterAddTrailersInTrailersCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    HeaderMapPtr trailers{new TestHeaderMapImpl{{"bazzz", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
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
        EXPECT_EQ(trailers.get(key), nullptr);
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
  decoder_filters_[0]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

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
        EXPECT_EQ(trailers.get(key), nullptr);
        return FilterTrailersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeTrailers(_));
  expectOnDestroy();

  // invoke encodeTrailers
  decoder_filters_[0]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(HttpConnectionManagerImplTest, FilterAddTrailersInDataCallbackNoTrailers) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
  }));

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
    ASSERT(t);
    EXPECT_EQ(t->value(), trailers_data.c_str());
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
  decoder_filters_[0]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

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
    EXPECT_EQ(t->value(), trailers_data.c_str());
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

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
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

// Don't send data frames, only headers and trailers.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback_NoDataFrames) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
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
  decoder_filters_[0]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

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
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

// Don't send data frames, only headers and trailers.
TEST_F(HttpConnectionManagerImplTest, FilterAddBodyInTrailersCallback_ContinueAfterCallback) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
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
  decoder_filters_[0]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);

  EXPECT_CALL(*encoder_filters_[0], encodeTrailers(_))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterTrailersStatus {
        encoder_filters_[0]->callbacks_->addEncodedData(trailers_data, false);
        return FilterTrailersStatus::StopIteration;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());

  decoder_filters_[0]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});

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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

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
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterClearRouteCache) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

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

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _))
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
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
  StreamDecoder* decoder;
  {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
      decoder = &conn_manager_->newStream(response_encoder_);
      // Call the high buffer callbacks as the codecs do.
      stream_callbacks_->onAboveWriteBufferHighWatermark();
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
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
      HeaderMapPtr headers{
          new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
      decoder->decodeHeaders(std::move(headers), true);
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
  StreamDecoder* decoder;
  {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
      decoder = &conn_manager_->newStream(response_encoder_);
      // Call the high buffer callbacks as the codecs do.
      stream_callbacks_->onAboveWriteBufferHighWatermark();
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
    EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
      HeaderMapPtr headers{
          new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
      decoder->decodeHeaders(std::move(headers), true);
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
}

TEST_F(HttpConnectionManagerImplTest, HitFilterWatermarkLimits) {
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
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

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
}

TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimits) {
  initial_buffer_limit_ = 10;
  streaming_filter_ = false;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Set the filter to be a buffering filter. Sending any data will hit the
  // watermark limit and result in a 413 being sent to the user.
  Http::TestHeaderMapImpl response_headers{
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
}

// Return 413 from an intermediate filter and make sure we don't continue the filter chain.
TEST_F(HttpConnectionManagerImplTest, HitRequestBufferLimitsIntermediateFilter) {
  InSequence s;
  initial_buffer_limit_ = 10;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  Http::TestHeaderMapImpl response_headers{
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

TEST_F(HttpConnectionManagerImplTest, HitResponseBufferLimitsBeforeHeaders) {
  initial_buffer_limit_ = 10;
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Start the response without processing the request headers through all
  // filters.
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early response.

  expectOnDestroy();
  Http::TestHeaderMapImpl expected_response_headers{
      {":status", "500"}, {"content-length", "21"}, {"content-type", "text/plain"}};
  Buffer::OwnedImpl fake_response("A long enough string to go over watermarks");
  // Fake response starts doing through the filter.
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
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
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Start the response, and make sure the request headers are fully processed.
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Now overload the buffer with response data. The filter returns
  // StopIterationAndBuffer, which will trigger an early reset.
  const std::string data = "A long enough string to go over watermarks";
  Buffer::OwnedImpl fake_response(data);
  InSequence s;
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(stream_, resetStream(_));
  EXPECT_LOG_CONTAINS(
      "debug", "Resetting stream. Response data too large and headers have already been sent",
      decoder_filters_[0]->callbacks_->encodeData(fake_response, false););

  EXPECT_EQ(1U, stats_.named_.rs_too_large_.value());
}

TEST_F(HttpConnectionManagerImplTest, FilterHeadReply) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "HEAD"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::Continue;
      }));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ("11", headers.ContentLength()->value().getStringView());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));
  expectOnDestroy();
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Verify that if an encoded stream has been ended, but gets stopped by a filter chain, we end
// up resetting the stream in the doEndStream() path (e.g., via filter reset due to timeout, etc.),
// we emit a reset to the codec.
TEST_F(HttpConnectionManagerImplTest, ResetWithStoppedFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  setupFilterChain(1, 1);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        decoder_filters_[0]->callbacks_->sendLocalReply(Code::BadRequest, "Bad request", nullptr,
                                                        absl::nullopt, "");
        return FilterHeadersStatus::Continue;
      }));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ("11", headers.ContentLength()->value().getStringView());
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterDataStatus {
        return FilterDataStatus::StopIterationAndBuffer;
      }));

  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_.stream_, resetStream(_));
  expectOnDestroy();
  encoder_filters_[0]->callbacks_->resetStream();
}

TEST_F(HttpConnectionManagerImplTest, FilterContinueAndEndStreamHeaders) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    auto headers = std::make_unique<TestHeaderMapImpl>(
        std::initializer_list<std::pair<std::string, std::string>>(
            {{":authority", "host"}, {":path", "/"}, {":method", "GET"}}));
    decoder->decodeHeaders(std::move(headers), false);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, true);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*encoder_filters_[1], encodeComplete());
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));

  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeHeaders(makeHeaderMap({{":status", "200"}}), true);

  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterContinueAndEndStreamData) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    auto headers = makeHeaderMap({{":authority", "host"}, {":path", "/"}, {":method", "GET"}});
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));

  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeHeaders(makeHeaderMap({{":status", "200"}}), false);

  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, true);
}

TEST_F(HttpConnectionManagerImplTest, FilterContinueAndEndStreamTrailers) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    auto headers = makeHeaderMap({{":authority", "host"}, {":path", "/"}, {":method", "GET"}});
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    auto trailers = makeHeaderMap({{"foo", "bar"}});
    decoder->decodeTrailers(std::move(trailers));
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::ContinueAndEndStream));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true));

  expectOnDestroy();

  decoder_filters_[1]->callbacks_->encodeHeaders(makeHeaderMap({{":status", "200"}}), false);

  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, false);

  auto response_trailers = makeHeaderMap({{"x-trailer", "1"}});
  decoder_filters_[1]->callbacks_->encodeTrailers(std::move(response_trailers));
}

TEST_F(HttpConnectionManagerImplTest, FilterAddBodyContinuation) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);

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
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

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
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, true);

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

  decoder_filters_[2]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);
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
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
  }));

  setupFilterChain(3, 3);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, true);

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

  decoder_filters_[2]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, true);

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
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
  }));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _));
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

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

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

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
  }));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _));
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

  decoder_filters_[1]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[1]->callbacks_->encodeData(response_body, false);
  decoder_filters_[1]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}});

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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, false);

    Buffer::OwnedImpl fake_data2("world");
    decoder->decodeData(fake_data2, true);
  }));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _));
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
  decoder_filters_[2]->callbacks_->encodeHeaders(
      HeaderMapPtr{new TestHeaderMapImpl{{":status", "200"}}}, false);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[2]->callbacks_->encodeData(response_body, false);
  decoder_filters_[2]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
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
}

TEST_F(HttpConnectionManagerImplTest, NoNewStreamWhenOverloaded) {
  setup(false, "");

  overload_manager_.overload_state_.setState(
      Server::OverloadActionNames::get().StopAcceptingRequests,
      Server::OverloadActionState::Active);

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
  }));

  // 503 direct response when overloaded.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("503", headers.Status()->value().getStringView());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("envoy overloaded", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_overload_close_.value());
}

TEST_F(HttpConnectionManagerImplTest, DisableKeepAliveWhenOverloaded) {
  setup(false, "");

  overload_manager_.overload_state_.setState(
      Server::OverloadActionNames::get().DisableHttpKeepAlive, Server::OverloadActionState::Active);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", "/"}, {":method", "GET"}, {"connection", "keep-alive"}}};
    decoder->decodeHeaders(std::move(headers), true);

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), true);

    data.drain(4);
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.Connection()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  EXPECT_EQ(1U, stats_.named_.downstream_cx_overload_disable_keepalive_.value());
}

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
  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Invoke encodeData while all iteration is stopped and make sure the filters do not have
  // encodeData called.
  EXPECT_CALL(*encoder_filters_[0], encodeData(_, _)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encodeData(_, _)).Times(0);
  Buffer::OwnedImpl response_body("response");
  decoder_filters_[0]->callbacks_->encodeData(response_body, false);
  decoder_filters_[0]->callbacks_->encodeTrailers(
      HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});

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

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":path", "/"}, {":method", "GET"}, {"connection", "keep-alive"}}};
    decoder->decodeHeaders(std::move(headers), true);

    HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->encodeHeaders(std::move(response_headers), true);

    data.drain(4);
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.Connection()->value().getStringView());
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestSessionTrace) {
  setup(false, "");

  // Set up the codec.
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance& data) -> void {
    data.drain(4);
  }));
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  setupFilterChain(1, 1);

  // Create a new stream
  StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);

  // Send headers to that stream, and verify we both set and clear the tracked object.
  {
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_))
        .Times(2)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> const ScopeTrackedObject* {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state, testing::HasSubstr("request_headers_: null"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
          return nullptr;
        }))
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
        .WillOnce(Invoke([](HeaderMap&, bool) -> FilterHeadersStatus {
          return FilterHeadersStatus::StopIteration;
        }));
    decoder->decodeHeaders(std::move(headers), false);
  }

  // Send trailers to that stream, and verify by this point headers are in logged state.
  {
    HeaderMapPtr trailers{new TestHeaderMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_))
        .Times(2)
        .WillOnce(Invoke([](const ScopeTrackedObject* object) -> const ScopeTrackedObject* {
          ASSERT(object != nullptr); // On the first call, this should be the active stream.
          std::stringstream out;
          object->dumpState(out);
          std::string state = out.str();
          EXPECT_THAT(state, testing::HasSubstr("request_headers_: \n"));
          EXPECT_THAT(state, testing::HasSubstr("':authority', 'host'\n"));
          EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));
          return nullptr;
        }))
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(*decoder_filters_[0], decodeComplete());
    EXPECT_CALL(*decoder_filters_[0], decodeTrailers(_))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
    decoder->decodeTrailers(std::move(trailers));
  }
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, decoder_filters_[0]->callbacks_->route());
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete()); // end_stream=true.

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
  }));
  const std::string fake_cluster1_name = "fake_cluster1";
  std::shared_ptr<Router::MockRoute> route1 = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route1->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster1_name));
  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster1 =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(fake_cluster1.get()));
  EXPECT_CALL(*route_config_, route(_, _, _)).WillOnce(Return(route1));
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
  EXPECT_CALL(*route_config1, route(_, _, _)).WillRepeatedly(Return(route1));
  EXPECT_CALL(*route_config2, route(_, _, _)).WillRepeatedly(Return(route2));
  EXPECT_CALL(*static_cast<const Router::MockScopedConfig*>(
                  scopedRouteConfigProvider()->config<Router::ScopedConfig>().get()),
              getRouteConfig(_))
      // 1. Snap scoped route config;
      // 2. refreshCachedRoute (both in decodeHeaders(headers,end_stream);
      // 3. then refreshCachedRoute triggered by decoder_filters_[1]->callbacks_->route().
      .Times(3)
      .WillRepeatedly(Invoke([&](const HeaderMap& headers) -> Router::ConfigConstSharedPtr {
        auto& test_headers = static_cast<const TestHeaderMapImpl&>(headers);
        if (test_headers.get_("scope_key") == "foo") {
          return route_config1;
        }
        return route_config2;
      }));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    StreamDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{new TestHeaderMapImpl{
        {":authority", "host"}, {":method", "GET"}, {"scope_key", "foo"}, {":path", "/foo"}}};
    decoder->decodeHeaders(std::move(headers), false);
    data.drain(4);
  }));
  setupFilterChain(2, 0);
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ(route1, decoder_filters_[0]->callbacks_->route());
        auto& test_headers = static_cast<TestHeaderMapImpl&>(headers);
        // Clear cached route and change scope key to "bar".
        decoder_filters_[0]->callbacks_->clearRouteCache();
        test_headers.remove("scope_key");
        test_headers.addCopy("scope_key", "bar");
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) -> FilterHeadersStatus {
        auto& test_headers = static_cast<TestHeaderMapImpl&>(headers);
        EXPECT_EQ(test_headers.get_("scope_key"), "bar");
        // Route now switched to route2 as header "scope_key" has changed.
        EXPECT_EQ(route2, decoder_filters_[1]->callbacks_->route());
        EXPECT_EQ(route2->routeEntry(), decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
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
      route(_, _, _))
      .WillOnce(Return(route1));
  StreamDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> void {
    decoder = &conn_manager_->newStream(response_encoder_);
    HeaderMapPtr headers{
        new TestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/foo"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
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

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponseUsingHttp3) {
  setup(false, "envoy-custom-server", false);

  filter_callbacks_.connection_.stream_info_.protocol_ = Envoy::Http::Protocol::Http3;
  codec_->protocol_ = Http::Protocol::Http3;
  EXPECT_EQ(Network::FilterStatus::StopIteration, conn_manager_->onNewConnection());

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](HeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.ForwardedProto()->value().getStringView());
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
  NiceMock<MockStreamEncoder> encoder;
  StreamDecoder& decoder = conn_manager_->newStream(encoder);
  HeaderMapPtr headers{
      new TestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  decoder.decodeHeaders(std::move(headers), true);

  HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->encodeHeaders(std::move(response_headers), true);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_cx_http3_total_.value());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  conn_manager_.reset();
  EXPECT_EQ(0U, stats_.named_.downstream_cx_http3_active_.value());
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
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> void {
    conn_manager_->newStream(response_encoder_);
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
}

} // namespace Http
} // namespace Envoy
