#pragma once

#include "common/http/conn_manager_impl.h"
#include "common/http/context_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/network/address_impl.h"

#include "extensions/access_loggers/common/file_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/delegating_route_utility.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

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

  HttpConnectionManagerImplTest();
  ~HttpConnectionManagerImplTest() override;
  Tracing::CustomTagConstSharedPtr requestHeaderCustomTag(const std::string& header);
  void setup(bool ssl, const std::string& server_name, bool tracing = true, bool use_srds = false);
  void setupFilterChain(int num_decoder_filters, int num_encoder_filters, int num_requests = 1);
  void setUpBufferLimits();

  // If request_with_data_and_trailers is true, includes data and trailers in the request. If
  // decode_headers_stop_all is true, decoder_filters_[0]'s callback decodeHeaders() returns
  // StopAllIterationAndBuffer.
  void setUpEncoderAndDecoder(bool request_with_data_and_trailers, bool decode_headers_stop_all);

  // Sends request headers, and stashes the new stream in decoder_;
  void startRequest(bool end_stream = false, absl::optional<std::string> body = absl::nullopt);

  Event::MockTimer* setUpTimer();
  void sendRequestHeadersAndData();
  ResponseHeaderMap* sendResponseHeaders(ResponseHeaderMapPtr&& response_headers);
  void expectOnDestroy(bool deferred = true);
  void doRemoteClose(bool deferred = true);

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
  std::chrono::milliseconds requestHeadersTimeout() const override {
    return request_headers_timeout_;
  }
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
  Http::StripPortType stripPortType() const override { return strip_port_type_; }
  const RequestIDExtensionSharedPtr& requestIDExtension() override { return request_id_extension_; }
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
  std::chrono::milliseconds request_headers_timeout_{};
  std::chrono::milliseconds delayed_close_timeout_{};
  absl::optional<std::chrono::milliseconds> max_stream_duration_{};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  RequestDecoder* decoder_{};
  std::shared_ptr<Ssl::MockConnectionInfo> ssl_connection_;
  std::shared_ptr<NiceMock<Tracing::MockHttpTracer>> tracer_{
      std::make_shared<NiceMock<Tracing::MockHttpTracer>>()};
  TracingConnectionManagerConfigPtr tracing_config_;
  SlowDateProviderImpl date_provider_{test_time_.timeSystem()};
  NiceMock<MockStream> stream_;
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
  Http::StripPortType strip_port_type_ = Http::StripPortType::None;
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  NiceMock<Network::MockClientConnection> upstream_conn_; // for websocket tests
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_; // for websocket tests
  RequestIDExtensionSharedPtr request_id_extension_;
  const LocalReply::LocalReplyPtr local_reply_;

  // TODO(mattklein123): Not all tests have been converted over to better setup. Convert the rest.
  NiceMock<MockResponseEncoder> response_encoder_;
  std::vector<MockStreamDecoderFilter*> decoder_filters_;
  std::vector<MockStreamEncoderFilter*> encoder_filters_;
  std::shared_ptr<AccessLog::MockInstance> log_handler_;
};

} // namespace Http
} // namespace Envoy
