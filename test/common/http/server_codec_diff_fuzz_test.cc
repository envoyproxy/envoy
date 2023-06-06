// This differential fuzzer compares behavior of HCM with two different codecs
// It generates wire bytes consisting of the request headers, body and trailers
// and submits it to two HCMs configured with different codecs.
// If the request is valid and HCM produces request headers, the test also
// generates response headers, body and trailers and submits them to both HCMs for encoding.
// The test expects that HTTP artifacts and output wire bytes produced by both HCMs are the same.

#include <algorithm>
#include <chrono>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "test/common/http/http2/http2_frame.h"
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

namespace Envoy {
namespace Http {

using Http2::Http2Frame;
using testing::InvokeWithoutArgs;
using testing::Return;

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

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillRepeatedly(Invoke([this](FilterChainManager& manager) -> bool {
          FilterFactoryCb decoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks) {
            callbacks.addStreamDecoderFilter(decoder_filter_);
          };
          FilterFactoryCb encoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks) {
            callbacks.addStreamEncoderFilter(encoder_filter_);
          };

          manager.applyFilterFactoryCb({}, decoder_filter_factory);
          manager.applyFilterFactoryCb({}, encoder_filter_factory);
          return true;
        }));
    EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
        .WillRepeatedly(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
          decoder_filter_->callbacks_ = &callbacks;
          callbacks.streamInfo().setResponseCodeDetails("");
        }));
    EXPECT_CALL(*encoder_filter_, setEncoderFilterCallbacks(_)).Times(testing::AtMost(1));
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
  bool shouldNormalizePath() const override { return normalize_path_; }
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
  std::shared_ptr<MockStreamDecoderFilter> decoder_filter_ =
      std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  std::shared_ptr<MockStreamEncoderFilter> encoder_filter_ =
      std::make_shared<NiceMock<MockStreamEncoderFilter>>();
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

class Http2HcmConfig : public HcmConfig {
public:
  enum class Http2Impl {
    Nghttp2,
    Oghttp2,
  };

  Http2HcmConfig(
      Http2Impl codec_impl,
      const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : HcmConfig(configuration) {
    http2_settings_.mutable_use_oghttp2_codec()->set_value(codec_impl == Http2Impl::Oghttp2);
    http2_settings_.set_allow_connect(true);
    http2_settings_ = Envoy::Http2::Utility::initializeAndValidateOptions(http2_settings_);
  }

  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance&,
                                  ServerConnectionCallbacks& callbacks,
                                  Server::OverloadManager& overload_manager) override {

    return std::make_unique<Http::Http2::ServerConnectionImpl>(
        connection, callbacks,
        Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *fake_stats_.rootScope()), random_,
        http2_settings_, maxRequestHeadersKb(), maxRequestHeadersCount(),
        headersWithUnderscoresAction(), overload_manager);
  }

  envoy::config::core::v3::Http2ProtocolOptions http2_settings_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
};

class HcmTestContext {
public:
  HcmTestContext(
      Http1ParserImpl http1_codec_impl,
      const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : config_(std::make_unique<Http1HcmConfig>(http1_codec_impl, configuration)),
        conn_manager_(*config_, drain_close_, random_, http_context_, runtime_, local_info_,
                      cluster_manager_, overload_manager_, config_->time_system_) {
    initialize();
  }

  HcmTestContext(
      Http2HcmConfig::Http2Impl http2_codec_impl,
      const test::common::http::ServerCodecDiffFuzzTestCase::Configuration& configuration)
      : config_(std::make_unique<Http2HcmConfig>(http2_codec_impl, configuration)),
        conn_manager_(*config_, drain_close_, random_, http_context_, runtime_, local_info_,
                      cluster_manager_, overload_manager_, config_->time_system_) {
    initialize();
  }

  void initialize() {
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

class HcmTest {
public:
  HcmTest(Http1ParserImpl codec1, Http1ParserImpl codec2,
          const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : hcm_under_test_1_(codec1, input.configuration()),
        hcm_under_test_2_(codec2, input.configuration()), input_(input) {}

  HcmTest(Http2HcmConfig::Http2Impl codec1, Http2HcmConfig::Http2Impl codec2,
          const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : hcm_under_test_1_(codec1, input.configuration()),
        hcm_under_test_2_(codec2, input.configuration()), input_(input) {}

  virtual ~HcmTest() = default;

  virtual void sendRequest() = 0;
  virtual void compareOutputWireBytes() = 0;

  void test() {
    sendRequest();

    // Check that both codecs either produced request headers or did not
    FUZZ_ASSERT((hcm_under_test_1_.config_->request_headers_ != nullptr &&
                 hcm_under_test_2_.config_->request_headers_ != nullptr) ||
                (hcm_under_test_1_.config_->request_headers_ == nullptr &&
                 hcm_under_test_2_.config_->request_headers_ == nullptr));

    if (hcm_under_test_1_.config_->request_headers_ != nullptr) {
      // When both codecs produced request headers they must be the same
      FUZZ_ASSERT(*hcm_under_test_1_.config_->request_headers_ ==
                  *hcm_under_test_2_.config_->request_headers_);
    }

    // If codecs produced request headers, send the response from the fuzzer input
    if (hcm_under_test_1_.config_->request_headers_ && input_.has_response()) {
      sendResponse();
    }

    // Check that both codecs either produced response headers or did not
    FUZZ_ASSERT((hcm_under_test_1_.config_->response_headers_ != nullptr &&
                 hcm_under_test_2_.config_->response_headers_ != nullptr) ||
                (hcm_under_test_1_.config_->response_headers_ == nullptr &&
                 hcm_under_test_2_.config_->response_headers_ == nullptr));

    // Check consistency of end stream flags across codecs
    FUZZ_ASSERT(hcm_under_test_1_.config_->request_end_stream_ ==
                hcm_under_test_2_.config_->request_end_stream_);
    FUZZ_ASSERT(hcm_under_test_1_.config_->response_end_stream_ ==
                hcm_under_test_2_.config_->response_end_stream_);

    if (hcm_under_test_1_.config_->response_headers_ != nullptr) {
      // When both codecs produced response headers they must be the same
      FUZZ_ASSERT(*hcm_under_test_1_.config_->response_headers_ ==
                  *hcm_under_test_2_.config_->response_headers_);
    }

    // Check connection state consistency across codecs
    FUZZ_ASSERT(hcm_under_test_1_.connection_alive_ == hcm_under_test_2_.connection_alive_);
    FUZZ_ASSERT(hcm_under_test_1_.out_end_stream_ == hcm_under_test_2_.out_end_stream_);
    // Check that wire output of both codecs was the same
    compareOutputWireBytes();
  }

  void sendResponse() {
    ResponseHeaderMapPtr response_headers_1 = makeResponseHeaders();
    ResponseHeaderMapPtr response_headers_2 =
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*response_headers_1);
    if (HeaderUtility::isSpecial1xx(*response_headers_1)) {
      hcm_under_test_1_.config_->decoder_filter_->callbacks_->encode1xxHeaders(
          std::move(response_headers_1));
      hcm_under_test_2_.config_->decoder_filter_->callbacks_->encode1xxHeaders(
          std::move(response_headers_2));
    } else {
      hcm_under_test_1_.config_->decoder_filter_->callbacks_->encodeHeaders(
          std::move(response_headers_1), true, "");
      hcm_under_test_2_.config_->decoder_filter_->callbacks_->encodeHeaders(
          std::move(response_headers_2), true, "");
    }
  }

  ResponseHeaderMapPtr makeResponseHeaders() {
    ASSERT(input_.has_response());
    ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
    if (input_.response().has_status()) {
      headers->setStatus(input_.response().status().value());
    }
    for (const auto& header : input_.response().headers()) {
      headers->addCopy(LowerCaseString(header.key()), header.value());
    }
    return headers;
  }

  void closeClientConnection() {
    hcm_under_test_1_.filter_callbacks_.connection_.raiseEvent(
        Network::ConnectionEvent::LocalClose);
    hcm_under_test_1_.filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    hcm_under_test_2_.filter_callbacks_.connection_.raiseEvent(
        Network::ConnectionEvent::LocalClose);
    hcm_under_test_2_.filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  HcmTestContext hcm_under_test_1_;
  HcmTestContext hcm_under_test_2_;
  const test::common::http::ServerCodecDiffFuzzTestCase input_;
};

class Http1HcmTest : public HcmTest {
public:
  Http1HcmTest(const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : HcmTest(Http1ParserImpl::HttpParser, Http1ParserImpl::BalsaParser, input) {}

  void sendRequest() override {
    std::string request = makeHttp1Request(input_.request());

    Buffer::OwnedImpl wire_input_1(request);
    hcm_under_test_1_.conn_manager_.onData(wire_input_1, true);

    Buffer::OwnedImpl wire_input_2(request);
    hcm_under_test_2_.conn_manager_.onData(wire_input_2, true);
  }

  void compareOutputWireBytes() override {
    FUZZ_ASSERT(hcm_under_test_1_.written_wire_bytes_ == hcm_under_test_2_.written_wire_bytes_);
  }

private:
  std::string
  makeHttp1Request(const test::common::http::ServerCodecDiffFuzzTestCase::Request& request) {
    std::string wire_bytes;
    wire_bytes =
        absl::StrCat(request.method().value(), " ", request.path().value(), " HTTP/1.1\r\n");
    if (request.has_authority()) {
      absl::StrAppend(&wire_bytes, "Host: ", request.authority().value(), "\r\n");
    }
    for (const auto& header : request.headers()) {
      absl::StrAppend(&wire_bytes, header.key(), ": ", header.value(), "\r\n");
    }
    absl::StrAppend(&wire_bytes, "\r\n");
    return wire_bytes;
  }
};

void printBuffer(absl::string_view buffer) {
  std::cout << "----------------\n";
  for (auto iter = buffer.cbegin(); iter != buffer.cend(); ++iter) {
    std::cout << fmt::format("{:02X} ", static_cast<const unsigned char>(*iter));
  }
  std::cout << std::endl;
}

class Http2HcmTest : public HcmTest {
public:
  Http2HcmTest(const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : HcmTest(Http2HcmConfig::Http2Impl::Nghttp2, Http2HcmConfig::Http2Impl::Oghttp2, input) {}

  void sendInitialFrames() {
    // Send preamble and the initial SETTINGS frame to the server
    std::string wire_bytes = makeStartingFrames();

    Buffer::OwnedImpl wire_input_1(wire_bytes);
    hcm_under_test_1_.conn_manager_.onData(wire_input_1, false);

    Buffer::OwnedImpl wire_input_2(wire_bytes);
    hcm_under_test_2_.conn_manager_.onData(wire_input_2, false);

    // The server codec sends SETTINGS, SETTINGS ACK and WINDOW_UPDATE frames.
    hcm_under_test_1_.written_wire_bytes_.clear();
    hcm_under_test_2_.written_wire_bytes_.clear();

    // Send ACK for the server SETTINGS
    wire_bytes = Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack).getStringView();
    Buffer::OwnedImpl ack_1(wire_bytes);
    hcm_under_test_1_.conn_manager_.onData(ack_1, false);

    Buffer::OwnedImpl ack_2(wire_bytes);
    hcm_under_test_2_.conn_manager_.onData(ack_2, false);
    FUZZ_ASSERT(hcm_under_test_1_.written_wire_bytes_.empty());
    FUZZ_ASSERT(hcm_under_test_2_.written_wire_bytes_.empty());
  }

  void sendRequest() override {
    sendInitialFrames();

    auto request = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0),
                                                     Http2Frame::HeadersFlags::EndHeaders);
    if (input_.request().has_scheme()) {
      request.appendHeaderWithoutIndexing(
          Http2Frame::Header(":scheme", input_.request().scheme().value()));
    }
    if (input_.request().has_method()) {
      request.appendHeaderWithoutIndexing(
          Http2Frame::Header(":method", input_.request().method().value()));
    }
    if (input_.request().has_path()) {
      request.appendHeaderWithoutIndexing(
          Http2Frame::Header(":path", input_.request().path().value()));
    }
    if (input_.request().has_authority()) {
      request.appendHeaderWithoutIndexing(
          Http2Frame::Header(":authority", input_.request().authority().value()));
    }
    if (input_.request().has_protocol()) {
      request.appendHeaderWithoutIndexing(
          Http2Frame::Header(":protocol", input_.request().protocol().value()));
    }

    for (const auto& header : input_.request().headers()) {
      request.appendHeaderWithoutIndexing(Http2Frame::Header(header.key(), header.value()));
    }
    request.adjustPayloadSize();

    Buffer::OwnedImpl wire_input_1(request.getStringView());
    hcm_under_test_1_.conn_manager_.onData(wire_input_1, false);

    Buffer::OwnedImpl wire_input_2(request.getStringView());
    hcm_under_test_2_.conn_manager_.onData(wire_input_2, false);
  }

  void compareOutputWireBytes() override {
    // First just compare bytes one to one
    if (hcm_under_test_1_.written_wire_bytes_ == hcm_under_test_2_.written_wire_bytes_) {
      return;
    }
    std::vector<Http2Frame> hcm_1_frames(
        parseNonControlFrames(hcm_under_test_1_.written_wire_bytes_));
    std::vector<Http2Frame> hcm_2_frames(
        parseNonControlFrames(hcm_under_test_1_.written_wire_bytes_));

    // If output bytes do not match, it does not indicate failure yet. These possibilities are
    // allowed:
    // 1. Both codecs emitted just control frames
    // 2. Both codecs emitted RST_STREAM
    // 3. Both codecs emitted HEADERS followed by RST_STREAM
    // 4. Both codecs emitted HEADERS
    if (hcm_1_frames.empty() && hcm_2_frames.empty()) {
      return;
    }
    FUZZ_ASSERT(hcm_1_frames.size() == hcm_2_frames.size());
    if (hcm_1_frames.front().type() == Http2Frame::Type::RstStream) {
      // No frames should be after RST_STREAM and should match RST_STREAM from second HCM
      FUZZ_ASSERT(hcm_1_frames.size() == 1 &&
                  hcm_2_frames.front().type() == Http2Frame::Type::RstStream);
      // Maybe check error code too?
    } else if (hcm_1_frames.front().type() == Http2Frame::Type::Headers) {
      FUZZ_ASSERT(hcm_2_frames.front().type() == Http2Frame::Type::Headers);
      std::vector<Http2Frame::Header> hcm_1_headers = hcm_1_frames.front().parseHeadersFrame();
      std::vector<Http2Frame::Header> hcm_2_headers = hcm_2_frames.front().parseHeadersFrame();
      // Headers should be the same
      FUZZ_ASSERT(hcm_1_headers.size() == hcm_2_headers.size() &&
                  std::equal(hcm_1_headers.begin(), hcm_1_headers.end(), hcm_2_headers.begin()));
      // HEADERS may be followed by RST_STREAM
      FUZZ_ASSERT(hcm_1_frames.size() == 2 &&
                  hcm_1_frames[1].type() == Http2Frame::Type::RstStream &&
                  hcm_2_frames[1].type() == Http2Frame::Type::RstStream);
    }
  }

private:
  std::string makeStartingFrames() {
    std::string wire_bytes;
    // Make preamble and empty SETTINGS frame
    wire_bytes = absl::StrCat(Http2::Http2Frame::Preamble,
                              Http2::Http2Frame::makeEmptySettingsFrame().getStringView());
    return wire_bytes;
  }

  std::vector<Http2Frame> parseNonControlFrames(absl::string_view wire_bytes) {
    std::vector<Http2Frame> frames;
    while (!wire_bytes.empty()) {
      Http2Frame frame = Http2Frame::makeGenericFrame(wire_bytes);
      const uint32_t frame_size = frame.frameSize();
      ASSERT(frame_size <= wire_bytes.size());
      if (frame.type() == Http2Frame::Type::Headers ||
          frame.type() == Http2Frame::Type::RstStream || frame.type() == Http2Frame::Type::Data) {
        frames.push_back(std::move(frame));
      }
      wire_bytes = wire_bytes.substr(frame_size);
    }
    return frames;
  }
};

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

  //  Http1HcmTest http1_test(input);
  //  http1_test.test();

  Http2HcmTest http2_test(input);
  http2_test.test();
}

} // namespace Http
} // namespace Envoy
