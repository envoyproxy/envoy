// This fuzzer explores the behavior of HCM with replay of trace actions that describe the behavior
// of a mocked codec and decoder/encoder filters. It is only partially complete (~60% test coverage
// with supplied corpus), since HCM has a lot of behavior to model, requiring investment in building
// out modeling actions and a corpus, which is time consuming and may not have significant security
// of functional correctness payoff beyond existing tests. Places where we could increase fuzz
// coverage include:
// * Watermarks
// * WebSocket upgrades
// * Tracing and stats.
// * Encode filter actions (e.g. modeling stop/continue, only done for decoder today).
// * SSL
// * Idle/drain timeouts.
// * HTTP 1.0 special cases
// * Fuzz config settings
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/common/empty_string.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/context_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/exception.h"
#include "common/http/header_utility.h"
#include "common/http/request_id_extension_impl.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/symbol_table_creator.h"

#include "test/common/http/conn_manager_impl_fuzz.pb.validate.h"
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

#include "gmock/gmock.h"

using testing::InvokeWithoutArgs;
using testing::Return;

namespace Envoy {
namespace Http {

class FuzzConfig : public ConnectionManagerConfig {
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

  FuzzConfig(envoy::extensions::filters::network::http_connection_manager::v3::
                 HttpConnectionManager::ForwardClientCertDetails forward_client_cert)
      : stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                        POOL_HISTOGRAM(fake_stats_))},
               "", fake_stats_),
        tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(fake_stats_))},
        listener_stats_{CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_stats_))},
        local_reply_(LocalReply::Factory::createDefault()) {
    ON_CALL(route_config_provider_, lastUpdated()).WillByDefault(Return(time_system_.systemTime()));
    ON_CALL(scoped_route_config_provider_, lastUpdated())
        .WillByDefault(Return(time_system_.systemTime()));
    access_logs_.emplace_back(std::make_shared<NiceMock<AccessLog::MockInstance>>());
    request_id_extension_ = RequestIDExtensionFactory::defaultInstance(random_);
    forward_client_cert_ = fromClientCert(forward_client_cert);
  }

  void newStream() {
    if (!codec_) {
      codec_ = new NiceMock<MockServerConnection>();
    }
    decoder_filter_ = new NiceMock<MockStreamDecoderFilter>();
    encoder_filter_ = new NiceMock<MockStreamEncoderFilter>();
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([this](FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{decoder_filter_});
          callbacks.addStreamEncoderFilter(StreamEncoderFilterSharedPtr{encoder_filter_});
        }));
    EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
        .WillOnce(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
          decoder_filter_->callbacks_ = &callbacks;
          callbacks.streamInfo().setResponseCodeDetails("");
        }));
    EXPECT_CALL(*encoder_filter_, setEncoderFilterCallbacks(_));
    EXPECT_CALL(filter_factory_, createUpgradeFilterChain("WebSocket", _, _))
        .WillRepeatedly(Invoke([&](absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                   FilterChainFactoryCallbacks& callbacks) -> bool {
          filter_factory_.createFilterChain(callbacks);
          return true;
        }));
  }

  Http::ForwardClientCertType
  fromClientCert(envoy::extensions::filters::network::http_connection_manager::v3::
                     HttpConnectionManager::ForwardClientCertDetails forward_client_cert) {
    switch (forward_client_cert) {
    case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        SANITIZE:
      return Http::ForwardClientCertType::Sanitize;
    case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        FORWARD_ONLY:
      return Http::ForwardClientCertType::ForwardOnly;
    case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        APPEND_FORWARD:
      return Http::ForwardClientCertType::AppendForward;
    case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        SANITIZE_SET:
      return Http::ForwardClientCertType::SanitizeSet;
    case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        ALWAYS_FORWARD_ONLY:
      return Http::ForwardClientCertType::AlwaysForwardOnly;
    default:
      return Http::ForwardClientCertType::Sanitize;
    }
  }

  // Http::ConnectionManagerConfig

  RequestIDExtensionSharedPtr requestIDExtension() override { return request_id_extension_; }
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
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
  std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
  std::chrono::milliseconds requestTimeout() const override { return request_timeout_; }
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
  Tracing::HttpTracerSharedPtr tracer() override { return http_tracer_; }
  const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get(); }
  ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  bool streamErrorOnInvalidHttpMessaging() const override {
    return stream_error_on_invalid_http_messaging_;
  }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return false; }
  bool shouldMergeSlashes() const override { return false; }
  bool shouldStripMatchingPort() const override { return false; }
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const override {
    return envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  }
  const LocalReply::LocalReply& localReply() const override { return *local_reply_; }

  const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      config_;
  NiceMock<Random::MockRandomGenerator> random_;
  RequestIDExtensionSharedPtr request_id_extension_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  MockServerConnection* codec_{};
  MockStreamDecoderFilter* decoder_filter_{};
  MockStreamEncoderFilter* encoder_filter_{};
  NiceMock<MockFilterChainFactory> filter_factory_;
  Event::SimulatedTimeSystem time_system_;
  SlowDateProviderImpl date_provider_{time_system_};
  bool use_srds_{};
  Router::MockRouteConfigProvider route_config_provider_;
  Router::MockScopedRouteConfigProvider scoped_route_config_provider_;
  std::string server_name_;
  HttpConnectionManagerProto::ServerHeaderTransformation server_transformation_{
      HttpConnectionManagerProto::OVERWRITE};
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
  std::chrono::milliseconds delayed_close_timeout_{};
  bool use_remote_address_{true};
  Http::ForwardClientCertType forward_client_cert_;
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  absl::optional<std::string> user_agent_;
  Tracing::HttpTracerSharedPtr http_tracer_{std::make_shared<NiceMock<Tracing::MockHttpTracer>>()};
  TracingConnectionManagerConfigPtr tracing_config_;
  bool proxy_100_continue_{true};
  bool stream_error_on_invalid_http_messaging_ = false;
  bool preserve_external_request_id_{false};
  Http::Http1Settings http1_settings_;
  Http::DefaultInternalAddressConfig internal_address_config_;
  bool normalize_path_{true};
  LocalReply::LocalReplyPtr local_reply_;
};

// Internal representation of stream state. Encapsulates the stream state, mocks
// and encoders for both the request/response.
class FuzzStream {
public:
  // We track stream state here to prevent illegal operations, e.g. applying an
  // encodeData() to the codec after encodeTrailers(). This is necessary to
  // maintain the preconditions for operations on the codec at the API level. Of
  // course, it's the codecs must be robust to wire-level violations. We
  // explore these violations via MutateAction and SwapAction at the connection
  // buffer level.
  enum class StreamState {
    PendingHeaders,
    PendingNonInformationalHeaders,
    PendingDataOrTrailers,
    Closed
  };

  FuzzStream(ConnectionManagerImpl& conn_manager, FuzzConfig& config,
             const HeaderMap& request_headers,
             test::common::http::HeaderStatus decode_header_status, bool end_stream)
      : conn_manager_(conn_manager), config_(config) {
    config_.newStream();
    request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
    response_state_ = StreamState::PendingHeaders;
    decoder_filter_ = config.decoder_filter_;
    encoder_filter_ = config.encoder_filter_;
    EXPECT_CALL(*config_.codec_, dispatch(_))
        .WillOnce(InvokeWithoutArgs([this, &request_headers, end_stream] {
          decoder_ = &conn_manager_.newStream(encoder_);
          auto headers = std::make_unique<TestRequestHeaderMapImpl>(request_headers);
          if (headers->Method() == nullptr) {
            headers->setReferenceKey(Headers::get().Method, "GET");
          }
          if (headers->Host() != nullptr &&
              !HeaderUtility::authorityIsValid(headers->getHostValue())) {
            // Sanitize host header so we don't fail at ASSERTs that verify header sanity checks
            // which should have been performed by the codec.
            headers->setHost(Fuzz::replaceInvalidHostCharacters(headers->getHostValue()));
          }
          // If sendLocalReply is called:
          ON_CALL(encoder_, encodeHeaders(_, true))
              .WillByDefault(Invoke([this](const ResponseHeaderMap&, bool end_stream) -> void {
                response_state_ =
                    end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
              }));
          decoder_->decodeHeaders(std::move(headers), end_stream);
          return Http::okStatus();
        }));
    ON_CALL(*decoder_filter_, decodeHeaders(_, _))
        .WillByDefault(InvokeWithoutArgs([this, decode_header_status,
                                          end_stream]() -> Http::FilterHeadersStatus {
          header_status_ = fromHeaderStatus(decode_header_status);
          // When a filter should not return ContinueAndEndStream when send with end_stream set
          // (see https://github.com/envoyproxy/envoy/pull/4885#discussion_r232176826)
          if (end_stream && (*header_status_ == Http::FilterHeadersStatus::ContinueAndEndStream)) {
            *header_status_ = Http::FilterHeadersStatus::Continue;
          }
          return *header_status_;
        }));
    fakeOnData();
    FUZZ_ASSERT(testing::Mock::VerifyAndClearExpectations(config_.codec_));
  }

  void fakeOnData() {
    Buffer::OwnedImpl fake_input;
    conn_manager_.onData(fake_input, false);
  }

  Http::FilterHeadersStatus fromHeaderStatus(test::common::http::HeaderStatus status) {
    switch (status) {
    case test::common::http::HeaderStatus::HEADER_CONTINUE:
      return Http::FilterHeadersStatus::Continue;
    case test::common::http::HeaderStatus::HEADER_STOP_ITERATION:
      return Http::FilterHeadersStatus::StopIteration;
    case test::common::http::HeaderStatus::HEADER_CONTINUE_AND_END_STREAM:
      return Http::FilterHeadersStatus::ContinueAndEndStream;
    case test::common::http::HeaderStatus::HEADER_STOP_ALL_ITERATION_AND_BUFFER:
      return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
    case test::common::http::HeaderStatus::HEADER_STOP_ALL_ITERATION_AND_WATERMARK:
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    default:
      return Http::FilterHeadersStatus::Continue;
    }
  }

  Http::FilterDataStatus fromDataStatus(test::common::http::DataStatus status) {
    switch (status) {
    case test::common::http::DataStatus::DATA_CONTINUE:
      return Http::FilterDataStatus::Continue;
    case test::common::http::DataStatus::DATA_STOP_ITERATION_AND_BUFFER:
      return Http::FilterDataStatus::StopIterationAndBuffer;
    case test::common::http::DataStatus::DATA_STOP_ITERATION_AND_WATERMARK:
      return Http::FilterDataStatus::StopIterationAndWatermark;
    case test::common::http::DataStatus::DATA_STOP_ITERATION_NO_BUFFER:
      return Http::FilterDataStatus::StopIterationNoBuffer;
    default:
      return Http::FilterDataStatus::Continue;
    }
  }

  Http::FilterTrailersStatus fromTrailerStatus(test::common::http::TrailerStatus status) {
    switch (status) {
    case test::common::http::TrailerStatus::TRAILER_CONTINUE:
      return Http::FilterTrailersStatus::Continue;
    case test::common::http::TrailerStatus::TRAILER_STOP_ITERATION:
      return Http::FilterTrailersStatus::StopIteration;
    default:
      return Http::FilterTrailersStatus::Continue;
    }
  }

  void decoderFilterCallbackAction(
      const test::common::http::DecoderFilterCallbackAction& decoder_filter_callback_action) {
    switch (decoder_filter_callback_action.decoder_filter_callback_action_selector_case()) {
    case test::common::http::DecoderFilterCallbackAction::kAddDecodedData: {
      if (request_state_ == StreamState::PendingDataOrTrailers) {
        Buffer::OwnedImpl buf(std::string(
            decoder_filter_callback_action.add_decoded_data().size() % (1024 * 1024), 'a'));
        decoder_filter_->callbacks_->addDecodedData(
            buf, decoder_filter_callback_action.add_decoded_data().streaming());
      }
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  void requestAction(StreamState& state, const test::common::http::RequestAction& request_action) {
    switch (request_action.request_action_selector_case()) {
    case test::common::http::RequestAction::kData: {
      if (state == StreamState::PendingDataOrTrailers) {
        const auto& data_action = request_action.data();
        ON_CALL(*decoder_filter_, decodeData(_, _))
            .WillByDefault(InvokeWithoutArgs([this, &data_action]() -> Http::FilterDataStatus {
              if (data_action.has_decoder_filter_callback_action()) {
                decoderFilterCallbackAction(data_action.decoder_filter_callback_action());
              }
              data_status_ = fromDataStatus(data_action.status());
              return *data_status_;
            }));
        EXPECT_CALL(*config_.codec_, dispatch(_)).WillOnce(InvokeWithoutArgs([this, &data_action] {
          Buffer::OwnedImpl buf(std::string(data_action.size() % (1024 * 1024), 'a'));
          decoder_->decodeData(buf, data_action.end_stream());
          return Http::okStatus();
        }));
        fakeOnData();
        FUZZ_ASSERT(testing::Mock::VerifyAndClearExpectations(config_.codec_));
        state = data_action.end_stream() ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      }
      break;
    }
    case test::common::http::RequestAction::kTrailers: {
      if (state == StreamState::PendingDataOrTrailers) {
        const auto& trailers_action = request_action.trailers();
        ON_CALL(*decoder_filter_, decodeTrailers(_))
            .WillByDefault(
                InvokeWithoutArgs([this, &trailers_action]() -> Http::FilterTrailersStatus {
                  if (trailers_action.has_decoder_filter_callback_action()) {
                    decoderFilterCallbackAction(trailers_action.decoder_filter_callback_action());
                  }
                  return fromTrailerStatus(trailers_action.status());
                }));
        EXPECT_CALL(*config_.codec_, dispatch(_))
            .WillOnce(InvokeWithoutArgs([this, &trailers_action] {
              decoder_->decodeTrailers(std::make_unique<TestRequestTrailerMapImpl>(
                  Fuzz::fromHeaders<TestRequestTrailerMapImpl>(trailers_action.headers())));
              return Http::okStatus();
            }));
        fakeOnData();
        FUZZ_ASSERT(testing::Mock::VerifyAndClearExpectations(config_.codec_));
        state = StreamState::Closed;
      }
      break;
    }
    case test::common::http::RequestAction::kContinueDecoding: {
      if (header_status_ == FilterHeadersStatus::StopAllIterationAndBuffer ||
          header_status_ == FilterHeadersStatus::StopAllIterationAndWatermark ||
          (header_status_ == FilterHeadersStatus::StopIteration &&
           (data_status_ == FilterDataStatus::StopIterationAndBuffer ||
            data_status_ == FilterDataStatus::StopIterationAndWatermark ||
            data_status_ == FilterDataStatus::StopIterationNoBuffer))) {
        decoder_filter_->callbacks_->continueDecoding();
      }
      break;
    }
    case test::common::http::RequestAction::kThrowDecoderException:
    // Dispatch no longer throws, execute subsequent kReturnDecoderError case.
    case test::common::http::RequestAction::kReturnDecoderError: {
      if (state == StreamState::PendingDataOrTrailers) {
        EXPECT_CALL(*config_.codec_, dispatch(_))
            .WillOnce(testing::Return(codecProtocolError("blah")));
        fakeOnData();
        FUZZ_ASSERT(testing::Mock::VerifyAndClearExpectations(config_.codec_));
        state = StreamState::Closed;
      }
      break;
    }
    default:
      // Maybe nothing is set or not a request action?
      break;
    }
  }

  void responseAction(StreamState& state,
                      const test::common::http::ResponseAction& response_action) {
    const bool end_stream = response_action.end_stream();
    switch (response_action.response_action_selector_case()) {
    case test::common::http::ResponseAction::kContinueHeaders: {
      if (state == StreamState::PendingHeaders) {
        auto headers = std::make_unique<TestResponseHeaderMapImpl>(
            Fuzz::fromHeaders<TestResponseHeaderMapImpl>(response_action.continue_headers()));
        headers->setReferenceKey(Headers::get().Status, "100");
        decoder_filter_->callbacks_->encode100ContinueHeaders(std::move(headers));
        // We don't allow multiple 100-continue headers in HCM, UpstreamRequest is responsible
        // for coalescing.
        state = StreamState::PendingNonInformationalHeaders;
      }
      break;
    }
    case test::common::http::ResponseAction::kHeaders: {
      if (state == StreamState::PendingHeaders ||
          state == StreamState::PendingNonInformationalHeaders) {
        auto headers = std::make_unique<TestResponseHeaderMapImpl>(
            Fuzz::fromHeaders<TestResponseHeaderMapImpl>(response_action.headers()));
        // The client codec will ensure we always have a valid :status.
        // Similarly, local replies should always contain this.
        uint64_t status;
        try {
          status = Utility::getResponseStatus(*headers);
        } catch (const CodecClientException&) {
          headers->setReferenceKey(Headers::get().Status, "200");
        }
        // The only 1xx header that may be provided to encodeHeaders() is a 101 upgrade,
        // guaranteed by the codec parsers. See include/envoy/http/filter.h.
        if (CodeUtility::is1xx(status) && status != enumToInt(Http::Code::SwitchingProtocols)) {
          headers->setReferenceKey(Headers::get().Status, "200");
        }
        decoder_filter_->callbacks_->encodeHeaders(std::move(headers), end_stream);
        state = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      }
      break;
    }
    case test::common::http::ResponseAction::kData: {
      if (state == StreamState::PendingDataOrTrailers) {
        Buffer::OwnedImpl buf(std::string(response_action.data() % (1024 * 1024), 'a'));
        decoder_filter_->callbacks_->encodeData(buf, end_stream);
        state = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      }
      break;
    }
    case test::common::http::ResponseAction::kTrailers: {
      if (state == StreamState::PendingDataOrTrailers) {
        decoder_filter_->callbacks_->encodeTrailers(std::make_unique<TestResponseTrailerMapImpl>(
            Fuzz::fromHeaders<TestResponseTrailerMapImpl>(response_action.trailers())));
        state = StreamState::Closed;
      }
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  void streamAction(const test::common::http::StreamAction& stream_action) {
    switch (stream_action.stream_action_selector_case()) {
    case test::common::http::StreamAction::kRequest: {
      requestAction(request_state_, stream_action.request());
      break;
    }
    case test::common::http::StreamAction::kResponse: {
      responseAction(response_state_, stream_action.response());
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  ConnectionManagerImpl& conn_manager_;
  FuzzConfig& config_;
  RequestDecoder* decoder_{};
  NiceMock<MockResponseEncoder> encoder_;
  MockStreamDecoderFilter* decoder_filter_{};
  MockStreamEncoderFilter* encoder_filter_{};
  StreamState request_state_;
  StreamState response_state_;
  absl::optional<Http::FilterHeadersStatus> header_status_;
  absl::optional<Http::FilterDataStatus> data_status_;
};

using FuzzStreamPtr = std::unique_ptr<FuzzStream>;

DEFINE_PROTO_FUZZER(const test::common::http::ConnManagerImplTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  } catch (const Envoy::ProtobufMessage::DeprecatedProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "DeprecatedProtoFieldException: {}", e.what());
    return;
  }

  FuzzConfig config(input.forward_client_cert());
  NiceMock<Network::MockDrainDecision> drain_close;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::SymbolTablePtr symbol_table(Stats::SymbolTableCreator::makeSymbolTable());
  Http::ContextImpl http_context(*symbol_table);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  auto ssl_connection = std::make_shared<Ssl::MockConnectionInfo>();
  bool connection_alive = true;

  ON_CALL(filter_callbacks.connection_, ssl()).WillByDefault(Return(ssl_connection));
  ON_CALL(Const(filter_callbacks.connection_), ssl()).WillByDefault(Return(ssl_connection));
  ON_CALL(filter_callbacks.connection_, close(_))
      .WillByDefault(InvokeWithoutArgs([&connection_alive] { connection_alive = false; }));
  filter_callbacks.connection_.local_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  filter_callbacks.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");

  ConnectionManagerImpl conn_manager(config, drain_close, random, http_context, runtime, local_info,
                                     cluster_manager, overload_manager, config.time_system_);
  conn_manager.initializeReadFilterCallbacks(filter_callbacks);

  std::vector<FuzzStreamPtr> streams;

  for (const auto& action : input.actions()) {
    ENVOY_LOG_MISC(trace, "action {} with {} streams", action.DebugString(), streams.size());
    if (!connection_alive) {
      ENVOY_LOG_MISC(trace, "skipping due to dead connection");
      break;
    }

    switch (action.action_selector_case()) {
    case test::common::http::Action::kNewStream: {
      streams.emplace_back(new FuzzStream(
          conn_manager, config,
          Fuzz::fromHeaders<TestRequestHeaderMapImpl>(action.new_stream().request_headers(),
                                                      /* ignore_headers =*/{}, {":authority"}),
          action.new_stream().status(), action.new_stream().end_stream()));
      break;
    }
    case test::common::http::Action::kStreamAction: {
      const auto& stream_action = action.stream_action();
      if (streams.empty()) {
        break;
      }
      (*std::next(streams.begin(), stream_action.stream_id() % streams.size()))
          ->streamAction(stream_action);
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  filter_callbacks.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  filter_callbacks.connection_.dispatcher_.clearDeferredDeleteList();
}

} // namespace Http
} // namespace Envoy
