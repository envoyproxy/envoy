#pragma once

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/header_validator.h"
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

struct SetupOpts {
  SetupOpts& setSsl(bool ssl) {
    ssl_ = ssl;
    return *this;
  }

  SetupOpts& setServerName(absl::string_view server_name) {
    server_name_ = server_name;
    return *this;
  }

  SetupOpts& setTracing(bool tracing) {
    tracing_ = tracing;
    return *this;
  }

  SetupOpts& setUseSrds(bool use_srds) {
    use_srds_ = use_srds;
    return *this;
  }

  SetupOpts& setHttp1SafeMaxConnectionDuration(bool http1_safe_max_connection_duration) {
    http1_safe_max_connection_duration_ = http1_safe_max_connection_duration;
    return *this;
  }

  bool ssl_{false};
  std::string server_name_{"envoy-server-test"};
  bool tracing_{true};
  bool use_srds_{false};
  bool http1_safe_max_connection_duration_{false};
};
// Base class for HttpConnectionManagerImpl related tests. This base class is used by tests under
// common/http as well as test/extensions/filters/http/ext_proc/, to reuse the many mocks/default
// impls of ConnectionManagerConfig that we need to provide to HttpConnectionManagerImpl.
class HttpConnectionManagerImplMixin : public ConnectionManagerConfig {
public:
  HttpConnectionManagerImplMixin();
  ~HttpConnectionManagerImplMixin() override;
  Tracing::CustomTagConstSharedPtr requestHeaderCustomTag(const std::string& header);
  void setup(const SetupOpts& opts = {});
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
  ResponseHeaderMap*
  sendResponseHeaders(ResponseHeaderMapPtr&& response_headers,
                      absl::optional<StreamInfo::CoreResponseFlag> response_flag = absl::nullopt,
                      std::string response_code_details = "details");
  void expectOnDestroy(bool deferred = true);
  void doRemoteClose(bool deferred = true);
  void testPathNormalization(const RequestHeaderMap& request_headers,
                             const ResponseHeaderMap& expected_response);

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  bool flushAccessLogOnNewRequest() override { return flush_access_log_on_new_request_; }
  bool flushAccessLogOnTunnelSuccessfullyEstablished() const override {
    return flush_log_on_tunnel_successfully_established_;
  }
  const absl::optional<std::chrono::milliseconds>& accessLogFlushInterval() override {
    return access_log_flush_interval_;
  }
  ServerConnectionPtr createCodec(Network::Connection&, const Buffer::Instance&,
                                  ServerConnectionCallbacks&, Server::OverloadManager&) override {
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
  bool http1SafeMaxConnectionDuration() const override {
    return http1_safe_max_connection_duration_;
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
  OptRef<const Router::ScopeKeyBuilder> scopeKeyBuilder() override {
    if (use_srds_) {
      return scope_key_builder_;
    }
    return {};
  }
  const std::string& serverName() const override { return server_name_; }
  HttpConnectionManagerProto::ServerHeaderTransformation
  serverHeaderTransformation() const override {
    return server_transformation_;
  }
  const absl::optional<std::string>& schemeToSet() const override { return scheme_; }
  bool shouldSchemeMatchUpstream() const override { return scheme_match_upstream_; }
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
  Tracing::TracerSharedPtr tracer() override { return tracer_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }
  ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  bool streamErrorOnInvalidHttpMessaging() const override {
    return stream_error_on_invalid_http_messaging_;
  }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return normalize_path_; }
  bool shouldMergeSlashes() const override { return merge_slashes_; }
  bool shouldStripTrailingHostDot() const override { return strip_trailing_host_dot_; }
  Http::StripPortType stripPortType() const override { return strip_port_type_; }
  const RequestIDExtensionSharedPtr& requestIDExtension() override { return request_id_extension_; }
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const override {
    return headers_with_underscores_action_;
  }
  const LocalReply::LocalReply& localReply() const override { return *local_reply_; }
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction
      pathWithEscapedSlashesAction() const override {
    return path_with_escaped_slashes_action_;
  }
  const std::vector<Http::OriginalIPDetectionSharedPtr>&
  originalIpDetectionExtensions() const override {
    return ip_detection_extensions_;
  }
  const std::vector<Http::EarlyHeaderMutationPtr>& earlyHeaderMutationExtensions() const override {
    return early_header_mutations_;
  }
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  const HttpConnectionManagerProto::ProxyStatusConfig* proxyStatusConfig() const override {
    return proxy_status_config_.get();
  }
  ServerHeaderValidatorPtr makeHeaderValidator(Protocol protocol) override {
    return header_validator_factory_.createServerHeaderValidator(protocol, header_validator_stats_);
  }
  bool appendLocalOverload() const override { return false; }
  bool appendXForwardedPort() const override { return false; }
  bool addProxyProtocolConnectionState() const override {
    return add_proxy_protocol_connection_state_;
  }

  // Simple helper to wrapper filter to the factory function.
  FilterFactoryCb createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamDecoderFilter(filter);
    };
  }
  FilterFactoryCb createEncoderFilterFactoryCb(StreamEncoderFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamEncoderFilter(filter);
    };
  }
  FilterFactoryCb createStreamFilterFactoryCb(StreamFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) { callbacks.addStreamFilter(filter); };
  }
  FilterFactoryCb createLogHandlerFactoryCb(AccessLog::InstanceSharedPtr handler) {
    return [handler](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addAccessLogHandler(handler);
    };
  }
  void expectUhvHeaderCheck(
      HeaderValidator::ValidationResult validation_result,
      ServerHeaderValidator::RequestHeadersTransformationResult transformation_result);
  void expectUhvTrailerCheck(HeaderValidator::ValidationResult validation_result,
                             HeaderValidator::TransformationResult transformation_result,
                             bool expect_response = true);
  // This method sets-up expectation that UHV will be called and provides real default UHV to
  // validate headers.
  void expectCheckWithDefaultUhv();

  Event::MockSchedulableCallback* enableStreamsPerIoLimit(uint32_t limit);

  Envoy::Event::SimulatedTimeSystem test_time_;
  NiceMock<Router::MockRouteConfigProvider> route_config_provider_;
  std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
  NiceMock<Router::MockScopedRouteConfigProvider> scoped_route_config_provider_;
  Router::MockScopeKeyBuilder scope_key_builder_;
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::IsolatedStoreImpl fake_stats_;
  Http::ContextImpl http_context_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  std::string access_log_path_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  bool flush_access_log_on_new_request_ = false;
  bool flush_log_on_tunnel_successfully_established_ = false;
  absl::optional<std::chrono::milliseconds> access_log_flush_interval_;
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
  absl::optional<std::string> scheme_;
  bool scheme_match_upstream_{false};
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  bool use_remote_address_{true};
  Http::DefaultInternalAddressConfig internal_address_config_;
  Http::ForwardClientCertType forward_client_cert_{Http::ForwardClientCertType::Sanitize};
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  absl::optional<std::string> user_agent_;
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  uint64_t max_requests_per_connection_{};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  bool http1_safe_max_connection_duration_{false};
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
  std::shared_ptr<NiceMock<Tracing::MockTracer>> tracer_{
      std::make_shared<NiceMock<Tracing::MockTracer>>()};
  TracingConnectionManagerConfigPtr tracing_config_;
  SlowDateProviderImpl date_provider_{test_time_.timeSystem()};
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
  std::vector<Http::OriginalIPDetectionSharedPtr> ip_detection_extensions_{};
  std::vector<Http::EarlyHeaderMutationPtr> early_header_mutations_{};
  bool add_proxy_protocol_connection_state_ = true;

  const LocalReply::LocalReplyPtr local_reply_;

  NiceMock<MockResponseEncoder> response_encoder_;
  std::vector<MockStreamDecoderFilter*> decoder_filters_;
  std::vector<MockStreamEncoderFilter*> encoder_filters_;
  std::shared_ptr<AccessLog::MockInstance> log_handler_;
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction path_with_escaped_slashes_action_{
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
              KEEP_UNCHANGED};
  bool strip_trailing_host_dot_ = false;
  std::unique_ptr<HttpConnectionManagerProto::ProxyStatusConfig> proxy_status_config_;
  NiceMock<MockHeaderValidatorFactory> header_validator_factory_;
  NiceMock<MockHeaderValidatorStats> header_validator_stats_;
  envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      header_validator_config_;
  Extensions::Http::HeaderValidators::EnvoyDefault::ConfigOverrides
      header_validator_config_overrides_;
};

class HttpConnectionManagerImplTest : public HttpConnectionManagerImplMixin,
                                      public testing::Test {};
} // namespace Http
} // namespace Envoy
