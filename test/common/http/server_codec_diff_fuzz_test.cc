// This differential fuzzer compares behavior of HCM with two different codecs
// It generates wire bytes consisting of the request headers, body and trailers
// and submits it to two HCMs configured with different codecs.
// If the request is valid and HCM produces request headers, the test also
// generates response headers, body and trailers and submits them to both HCMs for encoding.
// The test expects that HTTP artifacts and output wire bytes produced by both HCMs are the same.

#include <chrono>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "test/common/http/server_codec_diff_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::InvokeWithoutArgs;
using testing::Return;

namespace Envoy {
namespace Http {

class MockDateProvider : public DateProvider {
public:
  MOCK_METHOD(void, setDateHeader, (ResponseHeaderMap&));
};

class HcmConfig : public ConnectionManagerConfig {
public:
  HcmConfig(const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(*fake_stats_.rootScope()),
                                        POOL_GAUGE(fake_stats_),
                                        POOL_HISTOGRAM(*fake_stats_.rootScope()))},
               "", *fake_stats_.rootScope()),
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))},
        listener_stats_{CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_stats_))},
        normalize_path_(configuration.normalize_path()),
        local_reply_(LocalReply::Factory::createDefault()) {
    ON_CALL(route_config_provider_, lastUpdated()).WillByDefault(Return(time_system_.systemTime()));
    ON_CALL(scoped_route_config_provider_, lastUpdated())
        .WillByDefault(Return(time_system_.systemTime()));
    access_logs_.emplace_back(std::make_shared<NiceMock<AccessLog::MockInstance>>());
    request_id_extension_ = Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(random_);

    decoder_filter_ = new NiceMock<MockStreamDecoderFilter>();
    encoder_filter_ = new NiceMock<MockStreamEncoderFilter>();

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([this](FilterChainManager& manager) -> bool {
          FilterFactoryCb decoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks) {
            callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{decoder_filter_});
          };
          FilterFactoryCb encoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks) {
            callbacks.addStreamEncoderFilter(StreamEncoderFilterSharedPtr{encoder_filter_});
          };

          manager.applyFilterFactoryCb({}, decoder_filter_factory);
          manager.applyFilterFactoryCb({}, encoder_filter_factory);
          return true;
        }));
    EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
        .WillOnce(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
          decoder_filter_->callbacks_ = &callbacks;
          callbacks.streamInfo().setResponseCodeDetails("");
        }));
    EXPECT_CALL(*encoder_filter_, setEncoderFilterCallbacks(_));
    EXPECT_CALL(filter_factory_, createUpgradeFilterChain(_, _, _))
        .WillRepeatedly(Invoke([&](absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                   FilterChainManager& manager) -> bool {
          return filter_factory_.createFilterChain(manager);
        }));

    EXPECT_CALL(*decoder_filter_, decodeHeaders(_, _))
        .WillRepeatedly(
            Invoke([&](RequestHeaderMap& headers, bool end_stream) -> FilterHeadersStatus {
              request_headers_ = createHeaderMap<RequestHeaderMapImpl>(headers);
              request_end_stream_ = end_stream;
              return FilterHeadersStatus::Continue;
            }));

    EXPECT_CALL(*encoder_filter_, encodeHeaders(_, _))
        .WillRepeatedly(
            Invoke([&](ResponseHeaderMap& headers, bool end_stream) -> FilterHeadersStatus {
              response_headers_ = createHeaderMap<ResponseHeaderMapImpl>(headers);
              response_end_stream_ = end_stream;
              return FilterHeadersStatus::Continue;
            }));

    ON_CALL(date_provider_, setDateHeader(_)).WillByDefault(Invoke([](ResponseHeaderMap& headers) {
      headers.setDate("Fri, 13 May 2023 00:00:00 GMT");
    }));
  }

  // Http::ConnectionManagerConfig
  const RequestIDExtensionSharedPtr& requestIDExtension() override { return request_id_extension_; }
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  bool flushAccessLogOnNewRequest() override { return flush_access_log_on_new_request_; }
  bool flushAccessLogOnTunnelSuccessfullyEstablished() const override {
    return flush_access_log_on_tunnel_successfully_established_;
  }
  const absl::optional<std::chrono::milliseconds>& accessLogFlushInterval() override {
    return access_log_flush_interval_;
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
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
  std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
  std::chrono::milliseconds requestTimeout() const override { return request_timeout_; }
  std::chrono::milliseconds requestHeadersTimeout() const override {
    return request_headers_timeout_;
  }
  std::chrono::milliseconds delayedCloseTimeout() const override { return delayed_close_timeout_; }
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
  bool shouldNormalizePath() const override { return false; }
  bool shouldMergeSlashes() const override { return false; }
  bool shouldStripTrailingHostDot() const override { return false; }
  Http::StripPortType stripPortType() const override { return Http::StripPortType::None; }
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const override {
    return envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  }
  const LocalReply::LocalReply& localReply() const override { return *local_reply_; }
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction
      pathWithEscapedSlashesAction() const override {
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        KEEP_UNCHANGED;
  }
  const std::vector<Http::OriginalIPDetectionSharedPtr>&
  originalIpDetectionExtensions() const override {
    return ip_detection_extensions_;
  }
  const std::vector<Http::EarlyHeaderMutationPtr>& earlyHeaderMutationExtensions() const override {
    return early_header_mutations_;
  }
  uint64_t maxRequestsPerConnection() const override { return 0; }
  const HttpConnectionManagerProto::ProxyStatusConfig* proxyStatusConfig() const override {
    return proxy_status_config_.get();
  }
  Http::ServerHeaderValidatorPtr makeHeaderValidator(Protocol) override {
    // TODO(yanavlasov): fuzz test interface should use the default validator, although this could
    // be changed too
    return nullptr;
  }
  bool appendXForwardedPort() const override { return false; }
  bool addProxyProtocolConnectionState() const override { return true; }

  NiceMock<Random::MockRandomGenerator> random_;
  RequestIDExtensionSharedPtr request_id_extension_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  bool flush_access_log_on_new_request_ = false;
  bool flush_access_log_on_tunnel_successfully_established_ = false;
  absl::optional<std::chrono::milliseconds> access_log_flush_interval_;
  MockStreamDecoderFilter* decoder_filter_{};
  MockStreamEncoderFilter* encoder_filter_{};
  NiceMock<MockFilterChainFactory> filter_factory_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<MockDateProvider> date_provider_;
  bool use_srds_{};
  NiceMock<Router::MockRouteConfigProvider> route_config_provider_;
  Router::MockScopedRouteConfigProvider scoped_route_config_provider_;
  Router::MockScopeKeyBuilder scope_key_builder_;
  std::string server_name_{"envoy"};
  HttpConnectionManagerProto::ServerHeaderTransformation server_transformation_{
      HttpConnectionManagerProto::OVERWRITE};
  absl::optional<std::string> scheme_;
  Stats::IsolatedStoreImpl fake_stats_;
  ConnectionManagerStats stats_;
  ConnectionManagerTracingStats tracing_stats_;
  ConnectionManagerListenerStats listener_stats_;
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  absl::optional<std::chrono::milliseconds> max_stream_duration_;
  std::chrono::milliseconds stream_idle_timeout_{};
  std::chrono::milliseconds request_timeout_{};
  std::chrono::milliseconds request_headers_timeout_{};
  std::chrono::milliseconds delayed_close_timeout_{};
  bool use_remote_address_{true};
  Http::ForwardClientCertType forward_client_cert_{Http::ForwardClientCertType::ForwardOnly};
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  absl::optional<std::string> user_agent_;
  Tracing::TracerSharedPtr tracer_{std::make_shared<NiceMock<Tracing::MockTracer>>()};
  TracingConnectionManagerConfigPtr tracing_config_;
  bool proxy_100_continue_{true};
  bool stream_error_on_invalid_http_messaging_ = false;
  bool preserve_external_request_id_{false};
  Http::Http1Settings http1_settings_;
  Http::DefaultInternalAddressConfig internal_address_config_;
  const bool normalize_path_{false};
  LocalReply::LocalReplyPtr local_reply_;
  std::vector<Http::OriginalIPDetectionSharedPtr> ip_detection_extensions_{};
  std::vector<Http::EarlyHeaderMutationPtr> early_header_mutations_;
  std::unique_ptr<HttpConnectionManagerProto::ProxyStatusConfig> proxy_status_config_;
  Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  RequestHeaderMapPtr request_headers_;
  ResponseHeaderMapPtr response_headers_;
  bool request_end_stream_{false};
  bool response_end_stream_{false};
};

class Http1HcmConfig : public HcmConfig {
public:
  Http1HcmConfig(
      Http1ParserImpl codec_impl,
      const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : HcmConfig(configuration) {
    http1_settings_.use_balsa_parser_ = codec_impl == Http1ParserImpl::BalsaParser;
  }

  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance&,
                                  ServerConnectionCallbacks& callbacks,
                                  Server::OverloadManager& overload_manager) override {

    return std::make_unique<Http::Http1::ServerConnectionImpl>(
        connection,
        Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *fake_stats_.rootScope()), callbacks,
        http1_settings_, maxRequestHeadersKb(), maxRequestHeadersCount(),
        headersWithUnderscoresAction(), overload_manager);
  }
};

class HcmTestContext {
public:
  HcmTestContext(
      Http1ParserImpl http1_codec_impl,
      const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : config_(std::make_unique<Http1HcmConfig>(http1_codec_impl, configuration)),
        conn_manager_(*config_, drain_close_, random_, http_context_, runtime_, local_info_,
                      cluster_manager_, overload_manager_, config_->time_system_) {

    ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_));
    ON_CALL(Const(filter_callbacks_.connection_), ssl()).WillByDefault(Return(ssl_connection_));
    ON_CALL(filter_callbacks_.connection_, close(_)).WillByDefault(InvokeWithoutArgs([&] {
      connection_alive_ = false;
    }));
    ON_CALL(filter_callbacks_.connection_, close(_, _)).WillByDefault(InvokeWithoutArgs([&] {
      connection_alive_ = false;
    }));
    ON_CALL(filter_callbacks_.connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool end_stream) {
          written_wire_bytes_.append(data.toString());
          data.drain(data.length());
          out_end_stream_ = end_stream;
        }));
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setLocalAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    conn_manager_.initializeReadFilterCallbacks(filter_callbacks_);
  }

  const std::unique_ptr<HcmConfig> config_;
  NiceMock<Network::MockDrainDecision> drain_close_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::SymbolTableImpl symbol_table_;
  Http::ContextImpl http_context_{symbol_table_};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  Ssl::ConnectionInfoConstSharedPtr ssl_connection_{
      std::make_shared<NiceMock<Ssl::MockConnectionInfo>>()};
  bool connection_alive_{true};
  std::string written_wire_bytes_;
  bool out_end_stream_{false};
  ConnectionManagerImpl conn_manager_;
};

std::string
makeHttp1Request(const test::common::http::ServerCodecDiffFuzzTestCase::Request& request) {
  std::string wire_bytes;
  wire_bytes = absl::StrCat(request.method().value(), " ", request.path().value(), " HTTP/1.1\r\n");
  if (request.has_authority()) {
    absl::StrAppend(&wire_bytes, "Host: ", request.authority().value(), "\r\n");
  }
  for (const auto& header : request.headers()) {
    absl::StrAppend(&wire_bytes, header.key(), ": ", header.value(), "\r\n");
  }
  absl::StrAppend(&wire_bytes, "\r\n");
  return wire_bytes;
}

DEFINE_PROTO_FUZZER(const test::common::http::ServerCodecDiffFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  } catch (const Envoy::ProtobufMessage::DeprecatedProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "DeprecatedProtoFieldException: {}", e.what());
    return;
  }

  std::string request = makeHttp1Request(input.request());

  HcmTestContext hcm_under_test_1(Http1ParserImpl::HttpParser, input.configuration());
  HcmTestContext hcm_under_test_2(Http1ParserImpl::BalsaParser, input.configuration());

  Buffer::OwnedImpl wire_input_1(request);
  hcm_under_test_1.conn_manager_.onData(wire_input_1, true);

  Buffer::OwnedImpl wire_input_2(request);
  hcm_under_test_2.conn_manager_.onData(wire_input_2, true);

  hcm_under_test_1.filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  hcm_under_test_1.filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  hcm_under_test_2.filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  hcm_under_test_2.filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  FUZZ_ASSERT((hcm_under_test_1.config_->request_headers_ != nullptr &&
               hcm_under_test_2.config_->request_headers_ != nullptr) ||
              (hcm_under_test_1.config_->request_headers_ == nullptr &&
               hcm_under_test_2.config_->request_headers_ == nullptr));

  FUZZ_ASSERT((hcm_under_test_1.config_->response_headers_ != nullptr &&
               hcm_under_test_2.config_->response_headers_ != nullptr) ||
              (hcm_under_test_1.config_->response_headers_ == nullptr &&
               hcm_under_test_2.config_->response_headers_ == nullptr));

  FUZZ_ASSERT(hcm_under_test_1.config_->request_end_stream_ ==
              hcm_under_test_2.config_->request_end_stream_);
  FUZZ_ASSERT(hcm_under_test_1.config_->response_end_stream_ ==
              hcm_under_test_2.config_->response_end_stream_);
  if (hcm_under_test_1.config_->request_headers_ != nullptr) {
    FUZZ_ASSERT(*hcm_under_test_1.config_->request_headers_ ==
                *hcm_under_test_2.config_->request_headers_);
  }

  if (hcm_under_test_1.config_->response_headers_ != nullptr) {
    FUZZ_ASSERT(*hcm_under_test_1.config_->response_headers_ ==
                *hcm_under_test_2.config_->response_headers_);
  }

  FUZZ_ASSERT(hcm_under_test_1.connection_alive_ == hcm_under_test_2.connection_alive_);
  FUZZ_ASSERT(hcm_under_test_1.out_end_stream_ == hcm_under_test_2.out_end_stream_);
  FUZZ_ASSERT(hcm_under_test_1.written_wire_bytes_ == hcm_under_test_2.written_wire_bytes_);
}

} // namespace Http
} // namespace Envoy
