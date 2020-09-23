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
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/thread_local_cluster.h"
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
using testing::Mock;
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
            if (log_handler_.get()) {
              callbacks.addAccessLogHandler(log_handler_);
            }
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
    EXPECT_CALL(stream_, bufferLimit()).WillOnce(Return(initial_buffer_limit_));
    EXPECT_CALL(stream_, addCallbacks(_))
        .WillOnce(Invoke(
            [&](Http::StreamCallbacks& callbacks) -> void { stream_callbacks_ = &callbacks; }));
    EXPECT_CALL(stream_, setFlushTimeout(_));
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
    decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
    return altered_response_headers;
  }

  void expectOnDestroy(bool deferred = true) {
    for (auto filter : decoder_filters_) {
      EXPECT_CALL(*filter, onPreDestroy());
    }
    {
      auto setup_filter_expect = [](MockStreamEncoderFilter* filter) {
        EXPECT_CALL(*filter, onPreDestroy());
      };
      std::for_each(encoder_filters_.rbegin(), encoder_filters_.rend(), setup_filter_expect);
    }

    for (auto filter : decoder_filters_) {
      EXPECT_CALL(*filter, onDestroy());
    }
    {
      auto setup_filter_expect = [](MockStreamEncoderFilter* filter) {
        EXPECT_CALL(*filter, onDestroy());
      };
      std::for_each(encoder_filters_.rbegin(), encoder_filters_.rend(), setup_filter_expect);
    }

    if (deferred) {
      EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    }
  }

  void doRemoteClose(bool deferred = true) {
    EXPECT_CALL(stream_, removeCallbacks(_));
    expectOnDestroy(deferred);
    filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
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
  ConnectionManagerTracingStats tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))};
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
  std::shared_ptr<AccessLog::MockInstance> log_handler_;
};

TEST_F(HttpConnectionManagerImplTest, TestFilterCanEnrichAccessLogs) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*filter, onPreDestroy()).WillOnce(Invoke([&]() {
    ProtobufWkt::Value metadata_value;
    metadata_value.set_string_value("value");
    ProtobufWkt::Struct metadata;
    metadata.mutable_fields()->insert({"field", metadata_value});
    filter->callbacks_->streamInfo().setDynamicMetadata("metadata_key", metadata);
  }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        auto dynamic_meta = stream_info.dynamicMetadata().filter_metadata().at("metadata_key");
        EXPECT_EQ("value", dynamic_meta.fields().at("field").string_value());
      }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

} // namespace Http
} // namespace Envoy
