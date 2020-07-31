#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/access_log/access_log_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/common/macros.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/context_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/request_id_extension_impl.h"
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
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
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
using testing::NiceMock;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

class HttpConnectionManagerImplTest : public testing::Test, public ConnectionManagerConfig {
public:
  struct RouteConfigProvider : public Router::RouteConfigProvider {
    RouteConfigProvider(TimeSource& time_source) : time_source_(time_source) {}

    // Router::RouteConfigProvider
    Router::ConfigConstSharedPtr config() override { return route_config_; }
    absl::optional<ConfigInfo> configInfo() const override { return {}; }
    SystemTime lastUpdated() const override { return time_source_.systemTime(); }
    void onConfigUpdate() override {}

    TimeSource& time_source_;
    std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  };

  HttpConnectionManagerImplTest()
      : http_context_(fake_stats_.symbolTable()), access_log_path_("dummy_path"),
        access_logs_{
            AccessLog::InstanceSharedPtr{new Extensions::AccessLoggers::File::FileAccessLog(
                access_log_path_, {},
                Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(), log_manager_)}},
        codec_(new NiceMock<MockServerConnection>()),
        stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_HISTOGRAM(fake_stats_))},
               "", fake_stats_),
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))},
        listener_stats_({CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_listener_stats_))}),
        request_id_extension_(RequestIDExtensionFactory::defaultInstance(random_)),
        local_reply_(LocalReply::Factory::createDefault()) {

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

  Tracing::CustomTagConstSharedPtr requestHeaderCustomTag(const std::string& header) {
    envoy::type::tracing::v3::CustomTag::Header headerTag;
    headerTag.set_name(header);
    return std::make_shared<Tracing::RequestHeaderCustomTag>(header, headerTag);
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
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 443);
    filter_callbacks_.connection_.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    conn_manager_ = std::make_unique<ConnectionManagerImpl>(
        *this, drain_close_, random_, http_context_, runtime_, local_info_, cluster_manager_,
        overload_manager_, test_time_.timeSystem());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);

    if (tracing) {
      envoy::type::v3::FractionalPercent percent1;
      percent1.set_numerator(100);
      envoy::type::v3::FractionalPercent percent2;
      percent2.set_numerator(10000);
      percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
      tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
          TracingConnectionManagerConfig{Tracing::OperationName::Ingress,
                                         {{":method", requestHeaderCustomTag(":method")}},
                                         percent1,
                                         percent2,
                                         percent1,
                                         false,
                                         256});
    }
  }

  void setupFilterChain(int num_decoder_filters, int num_encoder_filters, int num_requests = 1) {
    // NOTE: The length/repetition in this routine allows InSequence to work correctly in an outer
    // scope.
    for (int i = 0; i < num_decoder_filters * num_requests; i++) {
      decoder_filters_.push_back(new MockStreamDecoderFilter());
    }

    for (int i = 0; i < num_encoder_filters * num_requests; i++) {
      encoder_filters_.push_back(new MockStreamEncoderFilter());
    }

    InSequence s;
    for (int req = 0; req < num_requests; req++) {
      EXPECT_CALL(filter_factory_, createFilterChain(_))
          .WillOnce(Invoke([num_decoder_filters, num_encoder_filters, req,
                            this](FilterChainFactoryCallbacks& callbacks) -> void {
            for (int i = 0; i < num_decoder_filters; i++) {
              callbacks.addStreamDecoderFilter(
                  StreamDecoderFilterSharedPtr{decoder_filters_[req * num_decoder_filters + i]});
            }

            for (int i = 0; i < num_encoder_filters; i++) {
              callbacks.addStreamEncoderFilter(
                  StreamEncoderFilterSharedPtr{encoder_filters_[req * num_encoder_filters + i]});
            }
          }));

      for (int i = 0; i < num_decoder_filters; i++) {
        EXPECT_CALL(*decoder_filters_[req * num_decoder_filters + i], setDecoderFilterCallbacks(_));
      }

      for (int i = 0; i < num_encoder_filters; i++) {
        EXPECT_CALL(*encoder_filters_[req * num_encoder_filters + i], setEncoderFilterCallbacks(_));
      }
    }
  }

  void setUpBufferLimits() {
    ON_CALL(response_encoder_, getStream()).WillByDefault(ReturnRef(stream_));
    EXPECT_CALL(stream_, addCallbacks(_))
        .WillOnce(Invoke(
            [&](Http::StreamCallbacks& callbacks) -> void { stream_callbacks_ = &callbacks; }));
    EXPECT_CALL(stream_, setFlushTimeout(_));
    EXPECT_CALL(stream_, bufferLimit()).WillOnce(Return(initial_buffer_limit_));
  }

  // If request_with_data_and_trailers is true, includes data and trailers in the request. If
  // decode_headers_stop_all is true, decoder_filters_[0]'s callback decodeHeaders() returns
  // StopAllIterationAndBuffer.
  void setUpEncoderAndDecoder(bool request_with_data_and_trailers, bool decode_headers_stop_all) {
    setUpBufferLimits();
    EXPECT_CALL(*codec_, dispatch(_))
        .WillOnce(Invoke([&, request_with_data_and_trailers](Buffer::Instance&) -> Http::Status {
          RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
          if (request_with_data_and_trailers) {
            decoder->decodeHeaders(std::move(headers), false);

            Buffer::OwnedImpl fake_data("12345");
            decoder->decodeData(fake_data, false);

            RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
            decoder->decodeTrailers(std::move(trailers));
          } else {
            decoder->decodeHeaders(std::move(headers), true);
          }
          return Http::okStatus();
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

  ResponseHeaderMap* sendResponseHeaders(ResponseHeaderMapPtr&& response_headers) {
    ResponseHeaderMap* altered_response_headers = nullptr;

    EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, _))
        .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
          altered_response_headers = &headers;
          return FilterHeadersStatus::Continue;
        }));
    EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
    decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
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
  std::chrono::milliseconds drainTimeout() const override { return std::chrono::milliseconds(100); }
  FilterChainFactory& filterFactory() override { return filter_factory_; }
  bool generateRequestId() const override { return true; }
  bool preserveExternalRequestId() const override { return false; }
  bool alwaysSetRequestIdInResponse() const override { return false; }
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
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
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
  const std::string& serverName() const override { return server_name_; }
  HttpConnectionManagerProto::ServerHeaderTransformation
  serverHeaderTransformation() const override {
    return server_transformation_;
  }
  ConnectionManagerStats& stats() override { return stats_; }
  ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() const override { return use_remote_address_; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return 0; }
  bool skipXffAppend() const override { return false; }
  const std::string& via() const override { return EMPTY_STRING; }
  Http::ForwardClientCertType forwardClientCert() const override { return forward_client_cert_; }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Network::Address::Instance& localAddress() override { return local_address_; }
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  Tracing::HttpTracerSharedPtr tracer() override { return tracer_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }
  ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  bool streamErrorOnInvalidHttpMessaging() const override {
    return stream_error_on_invalid_http_messaging_;
  }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return normalize_path_; }
  bool shouldMergeSlashes() const override { return merge_slashes_; }
  bool shouldStripMatchingPort() const override { return strip_matching_port_; }
  RequestIDExtensionSharedPtr requestIDExtension() override { return request_id_extension_; }
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const override {
    return headers_with_underscores_action_;
  }
  const LocalReply::LocalReply& localReply() const override { return *local_reply_; }

  Envoy::Event::SimulatedTimeSystem test_time_;
  NiceMock<Router::MockRouteConfigProvider> route_config_provider_;
  std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  NiceMock<Router::MockScopedRouteConfigProvider> scoped_route_config_provider_;
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
  absl::optional<std::chrono::milliseconds> max_stream_duration_{};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl_connection_;
  std::shared_ptr<NiceMock<Tracing::MockHttpTracer>> tracer_{
      std::make_shared<NiceMock<Tracing::MockHttpTracer>>()};
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
  bool stream_error_on_invalid_http_messaging_ = false;
  bool preserve_external_request_id_ = false;
  Http::Http1Settings http1_settings_;
  bool normalize_path_ = false;
  bool merge_slashes_ = false;
  bool strip_matching_port_ = false;
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  NiceMock<Network::MockClientConnection> upstream_conn_; // for websocket tests
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_; // for websocket tests
  RequestIDExtensionSharedPtr request_id_extension_;
  const LocalReply::LocalReplyPtr local_reply_;

  // TODO(mattklein123): Not all tests have been converted over to better setup. Convert the rest.
  MockResponseEncoder response_encoder_;
  std::vector<MockStreamDecoderFilter*> decoder_filters_;
  std::vector<MockStreamEncoderFilter*> encoder_filters_;
};

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .Times(2)
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
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
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        if (data.length() == 4) {
          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
          decoder->decodeHeaders(std::move(headers), true);
        } else {
          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/healthcheck"}, {":method", "GET"}}};
          decoder->decodeHeaders(std::move(headers), true);
        }

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        // Drain 2 so that on the 2nd iteration we will hit zero.
        data.drain(2);
        return Http::okStatus();
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
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
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
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
        filter->callbacks_->encode100ContinueHeaders(std::move(continue_headers));
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
        return Http::okStatus();
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
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
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
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
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
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
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
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(std::move(response_headers), false);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10923.
TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithDecoderPause) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  // Allow headers to pass.
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillRepeatedly(
          InvokeWithoutArgs([]() -> FilterHeadersStatus { return FilterHeadersStatus::Continue; }));
  // Pause and then resume the decode pipeline, this is key to triggering #10923.
  EXPECT_CALL(*filter, decodeData(_, false)).WillOnce(InvokeWithoutArgs([]() -> FilterDataStatus {
    return FilterDataStatus::StopIterationAndBuffer;
  }));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillRepeatedly(
          InvokeWithoutArgs([]() -> FilterDataStatus { return FilterDataStatus::Continue; }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        // Test not charging stats on the second call.
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder->decodeHeaders(std::move(headers), false);
        // Allow the decode pipeline to pause.
        decoder->decodeData(data, false);

        ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
        filter->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

        // Resume decode pipeline after encoding 100 continue headers, we're now
        // ready to trigger #10923.
        decoder->decodeData(data, true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
        return Http::okStatus();
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

// By default, Envoy will set the server header to the server name, here "custom-value"
TEST_F(HttpConnectionManagerImplTest, ServerHeaderOverwritten) {
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("custom-value", altered_headers->getServerValue());
}

// When configured APPEND_IF_ABSENT if the server header is present it will be retained.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendPresent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->getServerValue());
}

// When configured APPEND_IF_ABSENT if the server header is absent the server name will be set.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendAbsent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}});
  EXPECT_EQ("custom-value", altered_headers->getServerValue());
}

// When configured PASS_THROUGH, the server name will pass through.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughPresent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->getServerValue());
}

// When configured PASS_THROUGH, the server header will not be added if absent.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughAbsent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}});
  EXPECT_TRUE(altered_headers->Server() == nullptr);
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "http://api.lyft.com/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
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
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("404", headers.getStatusValue());
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"},
        {":path", "/ab%00c"}, // "%00" is not valid in path according to RFC
        {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
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
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
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
      .WillRepeatedly(Invoke([&](RequestHeaderMap& header_map, bool) -> FilterHeadersStatus {
        EXPECT_EQ(normalized_path, header_map.getPathValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*filter, onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// The router observes normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, RouteShouldUseSantizedPath) {
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  const std::string fake_cluster_name = "fake_cluster";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  std::shared_ptr<Router::MockRoute> route = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Invoke([&](const Router::RouteCallback&, const Http::RequestHeaderMap& header_map,
                           const StreamInfo::StreamInfo&, uint64_t) {
        EXPECT_EQ(normalized_path, header_map.getPathValue());
        return route;
      }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks&) -> void {}));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RouteOverride) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(2, 0);
  const std::string foo_bar_baz_cluster_name = "foo_bar_baz";
  const std::string foo_bar_cluster_name = "foo_bar";
  const std::string foo_cluster_name = "foo";
  const std::string default_cluster_name = "default";

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_bar_baz_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_bar_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(absl::string_view{foo_bar_cluster_name}))
      .WillOnce(Return(foo_bar_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  std::shared_ptr<Upstream::MockThreadLocalCluster> default_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(absl::string_view{default_cluster_name}))
      .Times(2)
      .WillRepeatedly(Return(default_cluster.get()));

  std::shared_ptr<Router::MockRoute> foo_bar_baz_route =
      std::make_shared<NiceMock<Router::MockRoute>>();

  std::shared_ptr<Router::MockRoute> foo_bar_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(foo_bar_route->route_entry_, clusterName()).WillOnce(ReturnRef(foo_bar_cluster_name));

  std::shared_ptr<Router::MockRoute> foo_route = std::make_shared<NiceMock<Router::MockRoute>>();

  std::shared_ptr<Router::MockRoute> default_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(default_route->route_entry_, clusterName())
      .Times(2)
      .WillRepeatedly(ReturnRef(default_cluster_name));

  using ::testing::InSequence;
  {
    InSequence seq;
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Return(default_route));

    // This filter iterates through all possible route matches and choose the last matched route
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

          // Not clearing cached route returns cached route and doesn't invoke cb.
          Router::RouteConstSharedPtr route = decoder_filters_[0]->callbacks_->route(
              [](Router::RouteConstSharedPtr, Router::RouteEvalStatus) -> Router::RouteMatchStatus {
                ADD_FAILURE() << "When route cache is not cleared CB should not be invoked";
                return Router::RouteMatchStatus::Accept;
              });
          EXPECT_EQ(default_route, route);

          int ctr = 0;
          const Router::RouteCallback& cb =
              [&](Router::RouteConstSharedPtr route,
                  Router::RouteEvalStatus route_eval_status) -> Router::RouteMatchStatus {
            EXPECT_LE(ctr, 3);
            if (ctr == 0) {
              ++ctr;
              EXPECT_EQ(foo_bar_baz_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 1) {
              ++ctr;
              EXPECT_EQ(foo_bar_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 2) {
              ++ctr;
              EXPECT_EQ(foo_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 3) {
              ++ctr;
              EXPECT_EQ(default_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::NoMoreRoutes);
              return Router::RouteMatchStatus::Accept;
            }
            return Router::RouteMatchStatus::Accept;
          };

          decoder_filters_[0]->callbacks_->clearRouteCache();
          route = decoder_filters_[0]->callbacks_->route(cb);

          EXPECT_EQ(default_route, route);
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

          return FilterHeadersStatus::Continue;
        }));

    // This route config expected to be invoked for all matching routes
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Invoke([&](const Router::RouteCallback& cb, const Http::RequestHeaderMap&,
                             const Envoy::StreamInfo::StreamInfo&,
                             uint64_t) -> Router::RouteConstSharedPtr {
          EXPECT_EQ(cb(foo_bar_baz_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_bar_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(default_route, Router::RouteEvalStatus::NoMoreRoutes),
                    Router::RouteMatchStatus::Accept);
          return default_route;
        }));

    EXPECT_CALL(*decoder_filters_[0], decodeComplete());

    // This filter chooses second route
    EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          EXPECT_EQ(default_route, decoder_filters_[1]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[1]->callbacks_->clusterInfo());

          int ctr = 0;
          const Router::RouteCallback& cb =
              [&](Router::RouteConstSharedPtr route,
                  Router::RouteEvalStatus route_eval_status) -> Router::RouteMatchStatus {
            EXPECT_LE(ctr, 1);
            if (ctr == 0) {
              ++ctr;
              EXPECT_EQ(foo_bar_baz_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 1) {
              ++ctr;
              EXPECT_EQ(foo_bar_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Accept;
            }
            return Router::RouteMatchStatus::Accept;
          };

          decoder_filters_[0]->callbacks_->clearRouteCache();
          decoder_filters_[1]->callbacks_->route(cb);

          EXPECT_EQ(foo_bar_route, decoder_filters_[1]->callbacks_->route());
          EXPECT_EQ(foo_bar_route->routeEntry(),
                    decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(foo_bar_cluster->info(), decoder_filters_[1]->callbacks_->clusterInfo());

          return FilterHeadersStatus::Continue;
        }));

    // This route config expected to be invoked for first two matching routes
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Invoke([&](const Router::RouteCallback& cb, const Http::RequestHeaderMap&,
                             const Envoy::StreamInfo::StreamInfo&,
                             uint64_t) -> Router::RouteConstSharedPtr {
          EXPECT_EQ(cb(foo_bar_baz_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_bar_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Accept);
          return foo_bar_route;
        }));

    EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  }

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Filters observe host header w/o port's part when port's removal is configured
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseNormalizedHost) {
  setup(false, "");
  // Enable port removal
  strip_matching_port_ = true;
  const std::string original_host = "host:443";
  const std::string normalized_host = "host";

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& header_map, bool) -> FilterHeadersStatus {
        EXPECT_EQ(normalized_host, header_map.getHostValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  EXPECT_CALL(*filter, onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// The router observes host header w/o port, not the original host, when
// remove_port is configured
TEST_F(HttpConnectionManagerImplTest, RouteShouldUseNormalizedHost) {
  setup(false, "");
  // Enable port removal
  strip_matching_port_ = true;
  const std::string original_host = "host:443";
  const std::string normalized_host = "host";

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  const std::string fake_cluster_name = "fake_cluster";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  std::shared_ptr<Router::MockRoute> route = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Invoke([&](const Router::RouteCallback&, const Http::RequestHeaderMap& header_map,
                           const StreamInfo::StreamInfo&, uint64_t) {
        EXPECT_EQ(normalized_host, header_map.getHostValue());
        return route;
      }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks&) -> void {}));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateNotSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const auto* modified_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  ASSERT_TRUE(modified_headers);
  EXPECT_TRUE(modified_headers->Date());
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateEnabledDateNotSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "true"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const auto* modified_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  ASSERT_TRUE(modified_headers);
  EXPECT_TRUE(modified_headers->Date());
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_NE(expected_date, modified_headers->getDateValue());
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateEnabledDateSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "true"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_EQ(expected_date, modified_headers->getDateValue());
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateFromCache) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  encoder_filters_[0]->callbacks_->streamInfo().setResponseFlag(
      StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_EQ(expected_date, modified_headers->getDateValue());
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  // No decorator.
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator())
      .WillRepeatedly(Return(nullptr));
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  struct TracingTagMetaSuite {
    using Factory =
        std::function<Tracing::CustomTagConstSharedPtr(const std::string&, const std::string&)>;
    std::string prefix;
    Factory factory;
  };
  struct TracingTagSuite {
    bool has_conn;
    bool has_route;
    std::list<Tracing::CustomTagConstSharedPtr> custom_tags;
    std::string tag;
    std::string tag_value;
  };
  std::vector<TracingTagMetaSuite> tracing_tag_meta_cases = {
      {"l-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Literal literal;
         literal.set_value(v);
         return std::make_shared<Tracing::LiteralCustomTag>(t, literal);
       }},
      {"e-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Environment e;
         e.set_default_value(v);
         return std::make_shared<Tracing::EnvironmentCustomTag>(t, e);
       }},
      {"x-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Header h;
         h.set_default_value(v);
         return std::make_shared<Tracing::RequestHeaderCustomTag>(t, h);
       }},
      {"m-tag", [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Metadata m;
         m.mutable_kind()->mutable_host();
         m.set_default_value(v);
         return std::make_shared<Tracing::MetadataCustomTag>(t, m);
       }}};
  std::vector<TracingTagSuite> tracing_tag_cases;
  for (const TracingTagMetaSuite& ms : tracing_tag_meta_cases) {
    const std::string& t1 = ms.prefix + "-1";
    const std::string& v1 = ms.prefix + "-v1";
    tracing_tag_cases.push_back({true, false, {ms.factory(t1, v1)}, t1, v1});

    const std::string& t2 = ms.prefix + "-2";
    const std::string& v2 = ms.prefix + "-v2";
    const std::string& rv2 = ms.prefix + "-r2";
    tracing_tag_cases.push_back({true, true, {ms.factory(t2, v2), ms.factory(t2, rv2)}, t2, rv2});

    const std::string& t3 = ms.prefix + "-3";
    const std::string& rv3 = ms.prefix + "-r3";
    tracing_tag_cases.push_back({false, true, {ms.factory(t3, rv3)}, t3, rv3});
  }
  Tracing::CustomTagMap conn_tracing_tags = {
      {":method", requestHeaderCustomTag(":method")}}; // legacy test case
  Tracing::CustomTagMap route_tracing_tags;
  for (TracingTagSuite& s : tracing_tag_cases) {
    if (s.has_conn) {
      const Tracing::CustomTagConstSharedPtr& ptr = s.custom_tags.front();
      conn_tracing_tags.emplace(ptr->tag(), ptr);
      s.custom_tags.pop_front();
    }
    if (s.has_route) {
      const Tracing::CustomTagConstSharedPtr& ptr = s.custom_tags.front();
      route_tracing_tags.emplace(ptr->tag(), ptr);
      s.custom_tags.pop_front();
    }
  }
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Ingress, conn_tracing_tags, percent1,
                                     percent2, percent1, false, 256});
  NiceMock<Router::MockRouteTracing> route_tracing;
  ON_CALL(route_tracing, getClientSampling()).WillByDefault(ReturnRef(percent1));
  ON_CALL(route_tracing, getRandomSampling()).WillByDefault(ReturnRef(percent2));
  ON_CALL(route_tracing, getOverallSampling()).WillByDefault(ReturnRef(percent1));
  ON_CALL(route_tracing, getCustomTags()).WillByDefault(ReturnRef(route_tracing_tags));
  ON_CALL(*route_config_provider_.route_config_->route_, tracingConfig())
      .WillByDefault(Return(&route_tracing));

  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  // Verify tag is set based on the request headers.
  EXPECT_CALL(*span, setTag(Eq(":method"), Eq("GET")));
  for (const TracingTagSuite& s : tracing_tag_cases) {
    EXPECT_CALL(*span, setTag(Eq(s.tag), Eq(s.tag_value)));
  }
  // Verify if the activeSpan interface returns reference to the current span.
  EXPECT_CALL(*span, setTag(Eq("service-cluster"), Eq("scoobydoo")));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Should be no 'x-envoy-decorator-operation' response header.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
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
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_EQ(true, route_config_provider_.route_config_->route_->decorator_.propagate());
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Verify decorator operation response header has been defined.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("testOp", headers.getEnvoyDecoratorOperationValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorPropagateFalse) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  ON_CALL(route_config_provider_.route_config_->route_->decorator_, propagate())
      .WillByDefault(Return(false));
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Verify decorator operation response header has NOT been defined (i.e. not propagated).
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorOverrideOp) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"},
                                         {"x-envoy-decorator-operation", "testOp"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  // Should be no 'x-envoy-decorator-operation' response header, as decorator
  // was overridden by request header.
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecorator) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_EQ(true, route_config_provider_.route_config_->route_->decorator_.propagate());
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.EnvoyDecoratorOperation());
        // Verify that decorator operation has been set as request header.
        EXPECT_EQ("testOp", headers.getEnvoyDecoratorOperationValue());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorPropagateFalse) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  ON_CALL(route_config_provider_.route_config_->route_->decorator_, propagate())
      .WillByDefault(Return(false));
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  // Verify that decorator operation has NOT been set as request header (propagate is false)
  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorOverrideOp) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"x-envoy-decorator-operation", "testOp"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest,
       StartAndFinishSpanNormalFlowEgressDecoratorOverrideOpNoActiveSpan) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"x-envoy-decorator-operation", "testOp"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
        return Http::okStatus();
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-forwarded-for", xff_address},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
        return Http::okStatus();
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
        EXPECT_EQ("downstream_remote_disconnect", stream_info.responseCodeDetails().value());
      }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), false);

        ResponseTrailerMapPtr response_trailers{new TestResponseTrailerMapImpl{{"x-trailer", "1"}}};
        filter->callbacks_->encodeTrailers(std::move(response_trailers));

        data.drain(4);
        return Http::okStatus();
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        // These request headers are missing the necessary ":host"
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);
        data.drain(0);
        return Http::okStatus();
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), false);

        ResponseTrailerMapPtr response_trailers{new TestResponseTrailerMapImpl{{"x-trailer", "1"}}};
        filter->callbacks_->encodeTrailers(std::move(response_trailers));

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, DoNotStartSpanIfTracingIsNotEnabled) {
  setup(false, "");

  // Disable tracing.
  tracing_config_.reset();

  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _)).Times(0);
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                             An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillByDefault(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, NoPath) {
  setup(false, "");

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "NOT_CONNECT"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("404", headers.getStatusValue());
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
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// When the global timeout is configured, the timer is enabled before we receive
// headers, if it fires we don't faceplant.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutGlobal) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    // Simulate and idle timeout so that the filter chain gets created.
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  // This should not be called as we don't have request headers.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _)).Times(0);

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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
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

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        Event::MockTimer* idle_timer = setUpTimer();
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(30), _));
        EXPECT_CALL(*idle_timer, disableTimer());
        decoder->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Per-route zero timeout overrides the global stream idle timeout.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutRouteZeroOverride) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(0)));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        Event::MockTimer* idle_timer = setUpTimer();
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        EXPECT_CALL(*idle_timer, disableTimer());
        decoder->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Validate the per-stream idle timeout after having sent downstream headers.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    data.drain(4);
    return Http::okStatus();
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
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
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
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
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
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
  RequestDecoder* decoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeHeaders(std::move(headers), false);

    ResponseHeaderMapPtr response_continue_headers{
        new TestResponseHeaderMapImpl{{":status", "100"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encode100ContinueHeaders(std::move(response_continue_headers));

    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);

    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeData(data, false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder->decodeTrailers(std::move(trailers));

    Buffer::OwnedImpl fake_response("world");
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeData(fake_response, false);

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 100 continue.
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutDisabledIfSetToZero) {
  request_timeout_ = std::chrono::milliseconds(0);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutValidlyConfigured) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
    EXPECT_CALL(*request_timer, disableTimer());

    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutCallbackDisarmsAndReturns408) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  std::string response_body;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(AtLeast(1));

    EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ("408", headers.getStatusValue());
        }));
    EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

    conn_manager_->newStream(response_encoder_);
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_)).Times(2);
    request_timer->invokeCallback();
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(1U, stats_.named_.downstream_rq_timeout_.value());
  EXPECT_EQ("request timeout", response_body);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsNotDisarmedOnIncompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(1);

    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    // the second parameter 'false' leaves the stream open
    decoder->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithData) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    decoder->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    decoder->decodeData(data, true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithTrailers) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
    decoder->decodeData(data, false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(1);
    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnConnectionTermination) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  Event::MockTimer* request_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");

  EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*request_timer, disableTimer()).Times(1);
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationDisabledIfSetToZero) {
  max_stream_duration_ = std::chrono::milliseconds(0);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationValidlyConfigured) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* duration_timer = setUpTimer();

    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _));
    EXPECT_CALL(*duration_timer, disableTimer());
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationCallbackResetStream) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup(false, "");
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(1);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*duration_timer, disableTimer());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  duration_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_rq_max_duration_reached_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_rx_reset_.value());
}

TEST_F(HttpConnectionManagerImplTest, Http10Rejected) {
  setup(false, "");
  RequestDecoder* decoder = nullptr;
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, protocol()).Times(AnyNumber()).WillRepeatedly(Return(Protocol::Http10));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("426", headers.getStatusValue());
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, Http10ConnCloseLegacy) {
  http1_settings_.accept_http_10_ = true;
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  RequestDecoder* decoder = nullptr;
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, protocol()).Times(AnyNumber()).WillRepeatedly(Return(Protocol::Http10));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host:80"}, {":method", "CONNECT"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ProxyConnectLegacyClose) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  RequestDecoder* decoder = nullptr;
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host:80"}, {":method", "CONNECT"}, {"proxy-connection", "close"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ConnectLegacyClose) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  RequestDecoder* decoder = nullptr;
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "CONNECT"}, {"connection", "close"}}};
    decoder->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationCallbackNotCalledIfResetStreamValidly) {
  max_stream_duration_ = std::chrono::milliseconds(5000);
  setup(false, "");
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(1);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*duration_timer, disableTimer());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_max_duration_reached_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_rx_reset_.value());
}

TEST_F(HttpConnectionManagerImplTest, RejectWebSocketOnNonWebSocketRoute) {
  setup(false, "");
  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                             {":method", "GET"},
                                                             {":path", "/"},
                                                             {"connection", "Upgrade"},
                                                             {"upgrade", "websocket"}}};
    decoder->decodeHeaders(std::move(headers), false);
    // Try sending trailers after the headers which will be rejected, just to
    // test the HCM logic that further decoding will not be passed to the
    // filters once the early response path is kicked off.
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"bazzz", "bar"}}};
    decoder->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("403", headers.getStatusValue());
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
      .WillRepeatedly(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, encodeHeaders(_, false))
      .WillRepeatedly(Invoke(
          [&](HeaderMap&, bool) -> FilterHeadersStatus { return FilterHeadersStatus::Continue; }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("upgrade", headers.getConnectionValue());
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
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                                 {":method", "GET"},
                                                                 {":path", "/"},
                                                                 {"connection", "Upgrade"},
                                                                 {"upgrade", "foo"}}};
        decoder->decodeHeaders(std::move(headers), false);

        filter->decoder_callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "101"}, {"Connection", "upgrade"}, {"upgrade", "foo"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(response_headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Make sure CONNECT requests hit the upgrade filter path.
TEST_F(HttpConnectionManagerImplTest, ConnectAsUpgrade) {
  setup(false, "envoy-custom-server", false);

  NiceMock<MockResponseEncoder> encoder;

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
        decoder->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, ConnectWithEmptyPath) {
  setup(false, "envoy-custom-server", false);

  NiceMock<MockResponseEncoder> encoder;

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", ""}, {":method", "CONNECT"}}};
        decoder->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ConnectLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.stop_faking_paths", "false"}});

  setup(false, "envoy-custom-server", false);

  NiceMock<MockResponseEncoder> encoder;
  RequestDecoder* decoder = nullptr;

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder = &conn_manager_->newStream(encoder);
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
        decoder->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(encoder, encodeHeaders(_, _))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("403", headers.getStatusValue());
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10138
TEST_F(HttpConnectionManagerImplTest, DrainCloseRaceWithClose) {
  InSequence s;
  setup(false, "");

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  expectOnDestroy();
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true);

  // Fake a protocol error that races with the drain timeout. This will cause a local close.
  // Also fake the local close not closing immediately.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Return(codecProtocolError("protocol error")));
  EXPECT_CALL(*drain_timer, disableTimer());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay))
      .WillOnce(Return());
  conn_manager_->onData(fake_input, false);

  // Now fire the close event which should have no effect as all close work has already been done.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest,
       FilterThatWaitsForBodyCanBeCalledAfterFilterThatAddsBodyEvenIfItIsNotLast) {
  InSequence s;
  setup(false, "");

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // 3 filters:
  // 1st filter adds a body
  // 2nd filter waits for the body
  // 3rd filter simulates router filter.
  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        Buffer::OwnedImpl body("body");
        decoder_filters_[0]->callbacks_->addDecodedData(body, false);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Invoke(
          [](Buffer::Instance&, bool) -> FilterDataStatus { return FilterDataStatus::Continue; }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Invoke([](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Invoke(
          [](Buffer::Instance&, bool) -> FilterDataStatus { return FilterDataStatus::Continue; }));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DrainClose) {
  setup(true, "");

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("https", headers.getForwardedProtoValue());
        return FilterHeadersStatus::StopIteration;
      }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("envoy-server-test", headers.getServerValue());
      }));
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true);
}

TEST_F(HttpConnectionManagerImplTest, DisconnectOnProxyConnectionDisconnect) {
  InSequence s;
  setup(false, "envoy-server-test");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "/"}, {":method", "GET"}, {"proxy-connection", "close"}}};
    decoder->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("close", headers.getConnectionValue());
        EXPECT_EQ(nullptr, headers.ProxyConnection());
      }));
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
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
  NiceMock<MockResponseEncoder> encoder;
  RequestDecoder* decoder = nullptr;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);

  // Start the response
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(encoder, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Server());
        EXPECT_EQ("", headers.getServerValue());
      }));
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), false);

  // Finish the request.
  EXPECT_CALL(*filter, decodeData(_, true));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder->decodeData(data, true);
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    conn_manager_->newStream(encoder);
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(encoder);
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
    decoder->decodeHeaders(std::move(headers), true);

    return codecProtocolError("protocol error");
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Verify that FrameFloodException causes connection to be closed abortively.
TEST_F(HttpConnectionManagerImplTest, FrameFloodError) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return bufferFloodError("too many outbound frames.");
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
  EXPECT_TRUE(filter_callbacks_.connection_.streamInfo().hasResponseFlag(
      StreamInfo::ResponseFlag::DownstreamProtocolError));
  EXPECT_EQ("codec error: too many outbound frames.",
            filter_callbacks_.connection_.streamInfo().responseCodeDetails().value());
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
    return Http::okStatus();
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
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
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

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(encoder);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
    return Http::okStatus();
  }));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationNoBuffer));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
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

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder->decodeHeaders(std::move(headers), false);

    Buffer::OwnedImpl fake_data("hello");
    decoder->decodeData(fake_data, true);
    return Http::okStatus();
  }));

  setupFilterChain(2, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIteration
