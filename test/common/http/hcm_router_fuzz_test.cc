#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"
#include "envoy/router/router.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/router/router.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/router/upstream_request.h"
#include "source/extensions/upstreams/http/http/upstream_request.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"

#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/common/http/hcm_router_fuzz.pb.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
// static Stats::TestUtil::TestSymbolTable gtst;
// static Stats::TestUtil::TestStore gts(*gtst);

// The set of singleton objects that hold state across multiple fuzz
// cases
class Harness;
class FuzzConfig;
class FuzzClusterManager;
static std::unique_ptr<Harness> harness = nullptr;
static std::unique_ptr<FuzzConfig> hcm_config = nullptr;
static std::unique_ptr<FuzzClusterManager> cluster_manager = nullptr;
static void cleanup() {
  cluster_manager = nullptr;
  harness = nullptr;
  hcm_config = nullptr;
}

using FuzzCase = test::common::http::RedirectFuzzCase;
using FuzzAction = test::common::http::FuzzAction;
using ActionCase = test::common::http::FuzzAction::ActionCase;

// An instance of this class will be installed in the filter chain
// for downstream connections by the `HTTP` connection manager.
class RouterFuzzFilter : public Router::Filter {
public:
  using Router::Filter::Filter;
  static StreamDecoderFilterSharedPtr create(Router::FilterConfig& config) {
    auto fuzz_filter = new RouterFuzzFilter(config, config.default_stats_);
    return StreamDecoderFilterSharedPtr{fuzz_filter};
  }
  // Filter
  Router::RetryStatePtr createRetryState(const Router::RetryPolicy&, RequestHeaderMap&,
                                         const Upstream::ClusterInfo&,
                                         const Router::VirtualCluster*,
                                         Router::RouteStatsContextOptRef, Runtime::Loader&,
                                         Random::RandomGenerator&, Event::Dispatcher&, TimeSource&,
                                         Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<Router::MockRetryState>();

    if (reject_all_hosts_) {
      // Set up RetryState to always reject the host
      ON_CALL(*retry_state_, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
    }
    if (retry_425_response_) {
      ON_CALL(*retry_state_, wouldRetryFromRetriableStatusCode(Code::TooEarly))
          .WillByDefault(Return(true));
    }
    return Router::RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  Router::MockRetryState* retry_state_{};
  bool reject_all_hosts_ = false;
  bool retry_425_response_ = false;
};

class FuzzConfig : public HttpConnectionManagerImplMixin {
public:
  FuzzConfig(Protobuf::RepeatedPtrField<std::string> strict_headers_to_check)
      : pool_(fake_stats_.symbolTable()), router_context_(fake_stats_.symbolTable()),
        shadow_writer_(new NiceMock<Router::MockShadowWriter>()),
        filter_config_(pool_.add("fuzz_filter"), local_info_, *fake_stats_.rootScope(), cm_,
                       runtime_, random_, Router::ShadowWriterPtr{shadow_writer_},
                       true /*emit_dynamic_stats*/, false /*start_child_span*/,
                       true /*suppress_envoy_headers*/, false /*respect_expected_rq_timeout*/,
                       true /*suppress_grpc_request_failure_code_stats*/,
                       false /*flush_upstream_log_on_upstream_stream*/,
                       std::move(strict_headers_to_check), time_system_.timeSystem(), http_context_,
                       router_context_) {}
  void newStream() {
    // Install the `RouterFuzzFilter` here
    ON_CALL(filter_factory_, createFilterChain(_))
        .WillByDefault(Invoke([this](FilterChainManager& manager) -> bool {
          FilterFactoryCb decoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks) {
            callbacks.addStreamDecoderFilter(RouterFuzzFilter::create(filter_config_));
          };
          manager.applyFilterFactoryCb({}, decoder_filter_factory);
          return true;
        }));
  }

  ServerConnectionPtr createCodec(Network::Connection&, const Buffer::Instance&,
                                  ServerConnectionCallbacks&, Server::OverloadManager&) {
    if (codec_ == nullptr) {
      codec_ = new NiceMock<MockServerConnection>();
    }
    ON_CALL(*codec_, dispatch(_)).WillByDefault(Return(Http::okStatus()));
    auto codec = ServerConnectionPtr{codec_};
    codec_ = nullptr;
    return codec;
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  Event::SimulatedTimeSystem time_system_;

private:
  Stats::StatNamePool pool_;
  Router::ContextImpl router_context_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Runtime::MockLoader> runtime_;
  Router::MockShadowWriter* shadow_writer_;
  Router::FilterConfig filter_config_;
};

// class FuzzConfig : public ConnectionManagerConfig {
// public:
//   FuzzConfig(Protobuf::RepeatedPtrField<std::string> strict_headers_to_check)
//       : stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(gts), POOL_GAUGE(gts),
//       POOL_HISTOGRAM(gts))},
//                "", *gts.rootScope()),
//         tracing_stats_{CONN_MAN_TRACING_STATS(POOL_COUNTER(gts))},
//         listener_stats_{CONN_MAN_LISTENER_STATS(POOL_COUNTER(gts))},
//         local_reply_(LocalReply::Factory::createDefault()), pool_(*gtst), router_context_(*gtst),
//         http_context_(*gtst), shadow_writer_(new Router::MockShadowWriter()),
//         filter_config_(pool_.add("test"), local_info_, *gts.rootScope(), cm_, runtime_, random_,
//                        Router::ShadowWriterPtr{shadow_writer_}, true, false /*start_child_span*/,
//                        true /*suppress_envoy_headers*/, false,
//                        true /*suppress_grpc_request_failure_code_stats*/,
//                        std::move(strict_headers_to_check), time_system_.timeSystem(),
//                        http_context_, router_context_) {
//
//     ON_CALL(route_config_provider_,
//     lastUpdated()).WillByDefault(Return(time_system_.systemTime()));
//     ON_CALL(scoped_route_config_provider_, lastUpdated())
//         .WillByDefault(Return(time_system_.systemTime()));
//     access_logs_.emplace_back(std::make_shared<NiceMock<AccessLog::MockInstance>>());
//     request_id_extension_ =
//     Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(random_); forward_client_cert_
//     = Http::ForwardClientCertType::ForwardOnly;
//   }
//
//   void newStream() {
//     // Install the `RouterFuzzFilter` here
//     ON_CALL(filter_factory_, createFilterChain(_))
//         .WillByDefault(Invoke([this](FilterChainManager& manager) -> bool {
//           FilterFactoryCb decoder_filter_factory = [this](FilterChainFactoryCallbacks& callbacks)
//           {
//             callbacks.addStreamDecoderFilter(RouterFuzzFilter::create(filter_config_));
//           };
//           manager.applyFilterFactoryCb({}, decoder_filter_factory);
//           return true;
//         }));
//   }
//
//   // Http::ConnectionManagerConfig
//   const RequestIDExtensionSharedPtr& requestIDExtension() override { return
//   request_id_extension_; } const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override {
//   return access_logs_; } bool flushAccessLogOnNewRequest() override { return
//   flush_access_log_on_new_request_; } const absl::optional<std::chrono::milliseconds>&
//   accessLogFlushInterval() override {
//     return access_log_flush_interval_;
//   }
//   ServerConnectionPtr createCodec(Network::Connection&, const Buffer::Instance&,
//                                   ServerConnectionCallbacks&) override {
//     MockServerConnection* codec = new MockServerConnection();
//     ON_CALL(*codec, dispatch(_)).WillByDefault(Return(Http::okStatus()));
//     return ServerConnectionPtr{codec};
//   }
//   DateProvider& dateProvider() override { return date_provider_; }
//   std::chrono::milliseconds drainTimeout() const override { return
//   std::chrono::milliseconds(100); } FilterChainFactory& filterFactory() override { return
//   filter_factory_; } bool generateRequestId() const override { return true; } bool
//   preserveExternalRequestId() const override { return false; } bool
//   alwaysSetRequestIdInResponse() const override { return false; } uint32_t maxRequestHeadersKb()
//   const override { return max_request_headers_kb_; } uint32_t maxRequestHeadersCount() const
//   override { return max_request_headers_count_; } absl::optional<std::chrono::milliseconds>
//   idleTimeout() const override { return idle_timeout_; } bool isRoutable() const override {
//   return true; } absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override
//   {
//     return max_connection_duration_;
//   }
//   absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
//     return max_stream_duration_;
//   }
//   std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
//   std::chrono::milliseconds requestTimeout() const override { return request_timeout_; }
//   std::chrono::milliseconds requestHeadersTimeout() const override {
//     return request_headers_timeout_;
//   }
//   std::chrono::milliseconds delayedCloseTimeout() const override { return delayed_close_timeout_;
//   } Router::RouteConfigProvider* routeConfigProvider() override {
//     if (use_srds_) {
//       return nullptr;
//     }
//     return &route_config_provider_;
//   }
//   Config::ConfigProvider* scopedRouteConfigProvider() override {
//     if (use_srds_) {
//       return &scoped_route_config_provider_;
//     }
//     return nullptr;
//   }
//   const std::string& serverName() const override { return server_name_; }
//   HttpConnectionManagerProto::ServerHeaderTransformation
//   serverHeaderTransformation() const override {
//     return server_transformation_;
//   }
//   const absl::optional<std::string>& schemeToSet() const override { return scheme_; }
//   ConnectionManagerStats& stats() override { return stats_; }
//   ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
//   bool useRemoteAddress() const override { return use_remote_address_; }
//   const Http::InternalAddressConfig& internalAddressConfig() const override {
//     return internal_address_config_;
//   }
//   uint32_t xffNumTrustedHops() const override { return 0; }
//   bool skipXffAppend() const override { return false; }
//   const std::string& via() const override { return EMPTY_STRING; }
//   Http::ForwardClientCertType forwardClientCert() const override { return forward_client_cert_; }
//   const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
//     return set_current_client_cert_details_;
//   }
//   const Network::Address::Instance& localAddress() override { return local_address_; }
//   const absl::optional<std::string>& userAgent() override { return user_agent_; }
//   Tracing::HttpTracerSharedPtr tracer() override { return http_tracer_; }
//   const TracingConnectionManagerConfig* tracingConfig() override { return tracing_config_.get();
//   } ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; } bool
//   proxy100Continue() const override { return proxy_100_continue_; } bool
//   streamErrorOnInvalidHttpMessaging() const override {
//     return stream_error_on_invalid_http_messaging_;
//   }
//   const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
//   bool shouldNormalizePath() const override { return false; }
//   bool shouldMergeSlashes() const override { return false; }
//   bool shouldStripTrailingHostDot() const override { return false; }
//   Http::StripPortType stripPortType() const override { return Http::StripPortType::None; }
//   envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
//   headersWithUnderscoresAction() const override {
//     return envoy::config::core::v3::HttpProtocolOptions::ALLOW;
//   }
//   const LocalReply::LocalReply& localReply() const override { return *local_reply_; }
//   envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
//       PathWithEscapedSlashesAction
//       pathWithEscapedSlashesAction() const override {
//     return
//     envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
//         KEEP_UNCHANGED;
//   }
//   const std::vector<Http::OriginalIPDetectionSharedPtr>&
//   originalIpDetectionExtensions() const override {
//     return ip_detection_extensions_;
//   }
//   const std::vector<Http::EarlyHeaderMutationPtr>& earlyHeaderMutationExtensions() const override
//   {
//     return early_header_mutations_;
//   }
//   uint64_t maxRequestsPerConnection() const override { return 0; }
//   const HttpConnectionManagerProto::ProxyStatusConfig* proxyStatusConfig() const override {
//     return proxy_status_config_.get();
//   }
//   Http::HeaderValidatorPtr makeHeaderValidator(Protocol) override {
//     // TODO(yanavlasov): fuzz test interface should use the default validator, although this
//     could
//     // be changed too
//     return nullptr;
//   }
//   bool appendXForwardedPort() const override { return false; }
//   bool addProxyProtocolConnectionState() const override {
//     return add_proxy_protocol_connection_state_;
//   }
//
//   const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
//       config_;
//   NiceMock<Random::MockRandomGenerator> random_;
//   RequestIDExtensionSharedPtr request_id_extension_;
//   std::list<AccessLog::InstanceSharedPtr> access_logs_;
//   bool flush_access_log_on_new_request_ = false;
//   absl::optional<std::chrono::milliseconds> access_log_flush_interval_;
//
//   Event::SimulatedTimeSystem time_system_;
//   SlowDateProviderImpl date_provider_{time_system_};
//   bool use_srds_{};
//   Router::MockRouteConfigProvider route_config_provider_;
//   Router::MockScopedRouteConfigProvider scoped_route_config_provider_;
//   std::string server_name_;
//   HttpConnectionManagerProto::ServerHeaderTransformation server_transformation_{
//       HttpConnectionManagerProto::OVERWRITE};
//   absl::optional<std::string> scheme_;
//   ConnectionManagerStats stats_;
//   ConnectionManagerTracingStats tracing_stats_;
//   ConnectionManagerListenerStats listener_stats_;
//   uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
//   uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
//   absl::optional<std::chrono::milliseconds> idle_timeout_;
//   absl::optional<std::chrono::milliseconds> max_connection_duration_;
//   absl::optional<std::chrono::milliseconds> max_stream_duration_;
//   std::chrono::milliseconds stream_idle_timeout_{};
//   std::chrono::milliseconds request_timeout_{};
//   std::chrono::milliseconds request_headers_timeout_{};
//   std::chrono::milliseconds delayed_close_timeout_{};
//   bool use_remote_address_{true};
//   Http::ForwardClientCertType forward_client_cert_;
//   std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
//   Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
//   absl::optional<std::string> user_agent_;
//   Tracing::HttpTracerSharedPtr http_tracer_{std::make_shared<NiceMock<Tracing::MockTracer>>()};
//   TracingConnectionManagerConfigPtr tracing_config_;
//   bool proxy_100_continue_{true};
//   bool stream_error_on_invalid_http_messaging_ = false;
//   bool preserve_external_request_id_{false};
//   Http::Http1Settings http1_settings_;
//   Http::DefaultInternalAddressConfig internal_address_config_;
//   bool normalize_path_{true};
//   LocalReply::LocalReplyPtr local_reply_;
//   std::vector<Http::OriginalIPDetectionSharedPtr> ip_detection_extensions_{};
//   std::vector<Http::EarlyHeaderMutationPtr> early_header_mutations_;
//   bool add_proxy_protocol_connection_state_ = true;
//   std::unique_ptr<HttpConnectionManagerProto::ProxyStatusConfig> proxy_status_config_;
//   NiceMock<MockFilterChainFactory> filter_factory_;
//
//   Stats::StatNamePool pool_;
//   Router::ContextImpl router_context_;
//   Http::ContextImpl http_context_;
//   Router::MockShadowWriter* shadow_writer_;
//
//   NiceMock<LocalInfo::MockLocalInfo> local_info_;
//   NiceMock<Upstream::MockClusterManager> cm_;
//   NiceMock<Runtime::MockLoader> runtime_;
//   Router::FilterConfig filter_config_;
// };

template <class T>
inline T
fromHeaders(const test::fuzz::Headers& headers,
            absl::node_hash_set<std::string> include_headers = absl::node_hash_set<std::string>()) {
  T header_map;
  for (const auto& header : headers.headers()) {
    header_map.addCopy(Fuzz::replaceInvalidCharacters(header.key()),
                       Fuzz::replaceInvalidCharacters(header.value()));
    include_headers.erase(absl::AsciiStrToLower(header.key()));
  }
  // Add dummy headers for non-present headers that must be included.
  for (const auto& header : include_headers) {
    header_map.addCopy(header, "dummy");
  }
  return header_map;
}

// We track stream state here to prevent illegal operations, e.g. applying an
// encodeData() to the codec after encodeTrailers(). This is necessary to
// maintain the preconditions for operations on the codec at the API level. Of
// course, it's the codecs must be robust to wire-level violations. We
// explore these violations via MutateAction and SwapAction at the connection
// buffer level.
enum class StreamState { PendingHeaders, PendingDataOrTrailers, Closed };

// This class mocks the downstream requests and serves as initial entry point
// for the fuzzer
class FuzzDownstream {
public:
  FuzzDownstream(ConnectionManagerImpl& conn_manager, FuzzConfig& config)
      : conn_manager_(conn_manager), config_(config), request_state_(StreamState::PendingHeaders) {
    config_.newStream();
    // If sendLocalReply is called:
    ON_CALL(encoder_, encodeHeaders(_, true))
        .WillByDefault(Invoke([this](const ResponseHeaderMap& headers, bool end_stream) -> void {
          std::string status_code = absl::StrCat("Status: ", headers.getStatusValue());
          request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
        }));
  }

  void sendHeaders(const test::fuzz::Headers& request_headers, bool end_stream,
                   absl::string_view path) {
    if (request_state_ == StreamState::PendingHeaders) {
      request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      auto h = fromHeaders<Http::TestRequestHeaderMapImpl>(request_headers, {"host"});
      auto headers = std::make_unique<TestRequestHeaderMapImpl>(h);
      if (headers->Method() == nullptr) {
        headers->setReferenceKey(Headers::get().Method, "GET");
      }
      headers->setReferenceKey(Headers::get().Path, path);

      decoder_ = &conn_manager_.newStream(encoder_);
      decoder_->decodeHeaders(std::move(headers), end_stream);
    }
  }

  void sendData(const std::string& request_data, bool end_stream) {
    if (request_state_ == StreamState::PendingDataOrTrailers) {
      request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      Buffer::OwnedImpl data(request_data);
      decoder_->decodeData(data, end_stream);
    }
  }

  void sendTrailers(const test::fuzz::Headers& request_trailers) {
    if (request_state_ == StreamState::PendingDataOrTrailers) {
      auto t = fromHeaders<TestRequestTrailerMapImpl>(request_trailers);
      auto trailers = std::make_unique<TestRequestTrailerMapImpl>(t);
      decoder_->decodeTrailers(std::move(trailers));
      request_state_ = StreamState::Closed;
    }
  }

  ConnectionManagerImpl& conn_manager_;
  FuzzConfig& config_;
  RequestDecoder* decoder_{};
  NiceMock<MockResponseEncoder> encoder_;
  StreamState request_state_;
};
using FuzzDownstreamPtr = std::unique_ptr<FuzzDownstream>;

// This class mocks the upstream and serves as entry point for responses.
class FuzzUpstream {
public:
  FuzzUpstream(Http::ResponseDecoder& decoder)
      : decoder_(decoder), response_state_(StreamState::PendingHeaders) {
    ON_CALL(mock_request_encoder_, encodeHeaders(_, _))
        .WillByDefault(Invoke([](const Http::RequestHeaderMap&, bool) { return okStatus(); }));
    ON_CALL(mock_request_encoder_.stream_, resetStream(_))
        .WillByDefault(
            Invoke([this](StreamResetReason) { response_state_ = StreamState::Closed; }));
  }

  void sendHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
    if (response_state_ == StreamState::PendingHeaders) {
      response_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      decoder_.decodeHeaders(std::move(headers), end_stream);
    }
  }

  void sendData(Buffer::Instance& data, bool end_stream) {
    if (response_state_ == StreamState::PendingDataOrTrailers) {
      response_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      decoder_.decodeData(data, end_stream);
    }
  }

  void sendTrailers(Http::ResponseTrailerMapPtr&& trailers) {
    if (response_state_ == StreamState::PendingDataOrTrailers) {
      response_state_ = StreamState::Closed;
      decoder_.decodeTrailers(std::move(trailers));
    }
  }

  Http::ResponseDecoder& decoder_;
  StreamState response_state_;
  NiceMock<MockRequestEncoder> mock_request_encoder_;
};

// This class mocks an upstream cluster. It holds the set of
// `FuzzUpstream` instances, which result from `FuzzDownstream`
// requests
class FuzzCluster {
public:
  FuzzCluster(FuzzConfig& cfg, std::string name, bool internal_redirect_policy_enabled,
              bool cross_scheme_redirect_allowed, bool allows_early_data_for_request)
      : cfg_(cfg), name_(name), mock_route_(new Router::MockRoute()), route_(mock_route_),
        internal_redirect_policy_enabled_(internal_redirect_policy_enabled),
        cross_scheme_redirect_allowed_(cross_scheme_redirect_allowed),
        allows_early_data_for_request_(allows_early_data_for_request), maintenance_(false) {
    ON_CALL(mock_route_->route_entry_, clusterName()).WillByDefault(ReturnRef(name_));
    ON_CALL(mock_route_->route_entry_.internal_redirect_policy_, enabled())
        .WillByDefault(Return(internal_redirect_policy_enabled_));
    ON_CALL(mock_route_->route_entry_.internal_redirect_policy_, shouldRedirectForResponseCode(_))
        .WillByDefault(
            Invoke([](const Http::Code& code) { return code == Http::Code::MovedPermanently; }));
    ON_CALL(mock_route_->route_entry_.internal_redirect_policy_, maxInternalRedirects())
        .WillByDefault(Return(10));
    ON_CALL(mock_route_->route_entry_.internal_redirect_policy_, isCrossSchemeRedirectAllowed())
        .WillByDefault(Return(cross_scheme_redirect_allowed_));
    ON_CALL(mock_route_->route_entry_.early_data_policy_, allowsEarlyDataForRequest(_))
        .WillByDefault(Return(allows_early_data_for_request_));
    ON_CALL(*tlc_.cluster_.info_.get(), maintenanceMode()).WillByDefault(Return(maintenance_));

    tlc_.cluster_.info_->upstream_config_ =
        std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
    tlc_.cluster_.info_->upstream_config_->set_name("envoy.filters.connection_pools.http.generic");
  }

  void newUpstream(Router::GenericConnectionPoolCallbacks* request,
                   absl::optional<Envoy::Http::Protocol> protocol) {
    auto upstream = std::make_unique<FuzzUpstream>(request->upstreamToDownstream());
    auto stream = std::make_unique<Extensions::Upstreams::Http::Http::HttpUpstream>(
        request->upstreamToDownstream(), &upstream->mock_request_encoder_);
    Upstream::HostDescriptionConstSharedPtr host =
        std::make_shared<Upstream::MockHostDescription>();
    request->onPoolReady(std::move(stream), host, mock_stream_info_.downstreamAddressProvider(),
                         mock_stream_info_, protocol);
    upstreams_.push_back(std::move(upstream));
  }

  void sendHeaders(uint32_t stream, const test::fuzz::Headers& response_headers, bool end_stream) {
    FuzzUpstream* s = select(stream);
    if (s) {
      auto h = fromHeaders<Http::TestResponseHeaderMapImpl>(response_headers, {"status"});
      auto headers = std::make_unique<TestResponseHeaderMapImpl>(h);
      uint64_t rc;
      if (!absl::SimpleAtoi(headers->getStatusValue(), &rc)) {
        headers->setStatus("302");
      }
      s->sendHeaders(std::move(headers), end_stream);
    }
  }

  void sendData(uint32_t stream, const std::string& data, bool end_stream) {
    FuzzUpstream* s = select(stream);
    if (s) {
      Buffer::OwnedImpl buf(data);
      s->sendData(buf, end_stream);
    }
  }

  void sendTrailers(uint32_t stream, const test::fuzz::Headers& response_trailers) {
    FuzzUpstream* s = select(stream);
    if (s) {
      auto t = fromHeaders<Http::TestResponseTrailerMapImpl>(response_trailers, {"status"});
      auto trailers = std::make_unique<TestResponseTrailerMapImpl>(t);
      s->sendTrailers(std::move(trailers));
    }
  }

  FuzzUpstream* select(uint32_t stream) {
    if (upstreams_.empty()) {
      return nullptr;
    }

    size_t idx = stream % upstreams_.size();
    return upstreams_[idx].get();
  }

  void addDirectResponse(Http::Code code, const std::string& body, const std::string& new_uri,
                         const std::string& route_name) {
    direct_response_entry_ = std::make_unique<Router::MockDirectResponseEntry>();
    ON_CALL(*direct_response_entry_.get(), responseCode()).WillByDefault(Return(code));
    ON_CALL(*direct_response_entry_.get(), responseBody()).WillByDefault(ReturnRef(body));
    ON_CALL(*direct_response_entry_.get(), newUri(_)).WillByDefault(Return(new_uri));
    ON_CALL(*direct_response_entry_.get(), routeName()).WillByDefault(ReturnRef(route_name));
    ON_CALL(*mock_route_, directResponseEntry())
        .WillByDefault(Return(direct_response_entry_.get()));
  }

  std::string getPath() const { return absl::StrCat("/", name_); }

  void reset() { upstreams_.clear(); }

  void reconfigure(bool internal_redirect, bool allow_cross_scheme, bool allow_early_data,
                   bool maintenance) {
    if (upstreams_.empty()) {
      internal_redirect_policy_enabled_ = internal_redirect;
      cross_scheme_redirect_allowed_ = allow_cross_scheme;
      allows_early_data_for_request_ = allow_early_data;
    }
    maintenance_ = maintenance;
  }

  FuzzConfig& cfg_;
  std::string name_;
  Upstream::MockThreadLocalCluster tlc_;
  Router::MockRoute* mock_route_;
  Router::RouteConstSharedPtr route_;
  bool internal_redirect_policy_enabled_;
  bool cross_scheme_redirect_allowed_;
  bool allows_early_data_for_request_;
  bool maintenance_;
  StreamInfo::MockStreamInfo mock_stream_info_;

  std::vector<std::unique_ptr<FuzzUpstream>> upstreams_;
  std::unique_ptr<Router::MockDirectResponseEntry> direct_response_entry_{};
};

// This class holds the upstream `FuzzCluster` instances. This has nothing
// to do with the cluster manager in envoy.
class FuzzClusterManager {
public:
  using FuzzClusterPtr = std::unique_ptr<FuzzCluster>;

  FuzzClusterManager(FuzzConfig& cfg) : cfg_(cfg) {
    ON_CALL(*cfg_.route_config_provider_.route_config_, route(_, _, _, _))
        .WillByDefault(Invoke(
            [this](const Router::RouteCallback&, const Http::RequestHeaderMap& request_map,
                   const Envoy::StreamInfo::StreamInfo&, uint64_t) { return route(request_map); }));
    ON_CALL(cfg_.cm_, getThreadLocalCluster(_))
        .WillByDefault(
            Invoke([this](absl::string_view cluster) { return getThreadLocalCluster(cluster); }));
  };

  void createDefaultClusters() {
    // Create a set of clusters which allows to model most possible scenarios
    FuzzClusterPtr default0 = std::make_unique<FuzzCluster>(cfg_, "default0", true, true, true);
    clusters_.push_back(std::move(default0));

    FuzzClusterPtr default1 = std::make_unique<FuzzCluster>(cfg_, "default1", true, true, true);
    clusters_.push_back(std::move(default1));

    FuzzClusterPtr default2 = std::make_unique<FuzzCluster>(cfg_, "default2", true, true, true);
    default2->addDirectResponse(Code::Found, "", "/default0", "/default0");
    clusters_.push_back(std::move(default2));
  }

  FuzzCluster& selectOneCluster(uint32_t selection) {
    return *clusters_[selection % clusters_.size()].get();
  }

  FuzzCluster* selectClusterByName(absl::string_view name) {
    for (auto& cluster : clusters_) {
      if (cluster->name_ == name) {
        return cluster.get();
      }
    }
    return nullptr;
  }

  void reset() {
    for (auto& cluster : clusters_) {
      cluster->reset();
    }
  }

private:
  Router::RouteConstSharedPtr route(const Http::RequestHeaderMap& request_map) {
    absl::string_view path = request_map.Path()->value().getStringView();
    std::vector<absl::string_view> v = absl::StrSplit(path, '/');
    if (v.size() < 2) {
      return nullptr;
    }
    FuzzCluster* cluster = selectClusterByName(v[1]);
    if (!cluster) {
      return nullptr;
    }
    return cluster->route_;
  }

  Upstream::ThreadLocalCluster* getThreadLocalCluster(absl::string_view name) {
    FuzzCluster* cluster = selectClusterByName(name);
    std::string sname(name.data(), name.size());
    if (!cluster) {
      return nullptr;
    }
    return &cluster->tlc_;
  }

  FuzzConfig& cfg_;
  std::vector<std::unique_ptr<FuzzCluster>> clusters_;
};

// Register a custom ConnPoolFactory, which will mock the upstream
// connections to the mock cluster management.
class FuzzGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.http.generic"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool(Upstream::ThreadLocalCluster&, bool is_connect,
                                                   const Router::RouteEntry& route_entry,
                                                   absl::optional<Envoy::Http::Protocol> protocol,
                                                   Upstream::LoadBalancerContext*) const override {
    if (is_connect) {
      return nullptr;
    }
    FuzzCluster* cluster = cluster_manager->selectClusterByName(route_entry.clusterName());
    if (cluster == nullptr) {
      return nullptr;
    }
    auto conn_pool = std::make_unique<Router::MockGenericConnPool>();
    ON_CALL(*conn_pool.get(), newStream(_))
        .WillByDefault(Invoke([cluster, protocol](Router::GenericConnectionPoolCallbacks* request) {
          cluster->newUpstream(request, protocol);
        }));
    return conn_pool;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::http::generic::v3::GenericConnectionPoolProto>();
  }
};

class Harness {
public:
  Harness(FuzzConfig& cfg) : hcm_config_(cfg), reg_(fuzz_conn_pool_factory_), closed_(false) {
    ON_CALL(filter_callbacks_.connection_, close(_, _)).WillByDefault(InvokeWithoutArgs([this]() {
      closed_ = true;
    }));
  }

  void fuzz(const FuzzCase& input) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
    scoped_runtime.mergeValues({{"envoy.reloadable_features.allow_upstream_filters", "false"}});
    hcm_ = std::make_unique<ConnectionManagerImpl>(
        hcm_config_, drain_close_, random_, hcm_config_.http_context_, runtime_, local_info_,
        hcm_config_.cm_, overload_manager_, hcm_config_.time_system_);
    hcm_->initializeReadFilterCallbacks(filter_callbacks_);
    Buffer::OwnedImpl data;
    hcm_->onData(data, false);

    for (const auto& action : input.actions()) {
      if (closed_) {
        break;
      }
      switch (action.action_case()) {
      case ActionCase::kAdvanceTime: {
        const auto& a = action.advance_time();
        hcm_config_.time_system_.timeSystem().advanceTimeWait(
            std::chrono::milliseconds(a.milliseconds()));
      } break;
      case ActionCase::kReconfigureCluster: {
        const auto& a = action.reconfigure_cluster();
        FuzzCluster& cluster = cluster_manager->selectOneCluster(a.cluster());
        cluster.reconfigure(a.enable_internal_redirect(), a.allow_cross_scheme_redirect(),
                            a.allow_early_data(), a.maintenance_mode());

      } break;
      case ActionCase::kRequestHeader: {
        const auto& a = action.request_header();
        FuzzCluster& cluster = cluster_manager->selectOneCluster(a.cluster());
        FuzzDownstreamPtr stream = std::make_unique<FuzzDownstream>(*hcm_.get(), hcm_config_);
        streams_.push_back(std::move(stream));
        streams_.back()->sendHeaders(a.headers(), a.end_stream(), cluster.getPath());
      } break;
      case ActionCase::kRequestData:
        if (!streams_.empty()) {
          const auto& a = action.request_data();
          FuzzDownstream& s = *streams_[a.stream() % streams_.size()].get();
          s.sendData(a.data(), a.end_stream());
        }
        break;
      case ActionCase::kRequestTrailer:
        if (!streams_.empty()) {
          const auto& a = action.request_trailer();
          FuzzDownstream& s = *streams_[a.stream() % streams_.size()].get();
          s.sendTrailers(a.trailers());
        }
        break;
      case ActionCase::kRespondHeader: {
        const auto& a = action.respond_header();
        FuzzCluster& cluster = cluster_manager->selectOneCluster(a.cluster());
        cluster.sendHeaders(a.stream(), a.headers(), a.end_stream());
      } break;
      case ActionCase::kRespondData: {
        const auto& a = action.respond_data();
        FuzzCluster& cluster = cluster_manager->selectOneCluster(a.cluster());
        cluster.sendData(a.stream(), a.data(), a.end_stream());
      } break;
      case ActionCase::kRespondTrailer: {
        const auto& a = action.respond_trailer();
        FuzzCluster& cluster = cluster_manager->selectOneCluster(a.cluster());
        cluster.sendTrailers(a.stream(), a.trailers());
      } break;
      default:
        break;
      }
    }
    hcm_->onEvent(Network::ConnectionEvent::RemoteClose);
  }

  void reset() {
    cluster_manager->reset();
    streams_.clear();
  }

private:
  FuzzConfig& hcm_config_;
  NiceMock<Network::MockDrainDecision> drain_close_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<ConnectionManagerImpl> hcm_;
  FuzzGenericConnPoolFactory fuzz_conn_pool_factory_;
  Registry::InjectFactory<Router::GenericConnPoolFactory> reg_;

  bool closed_;
  std::vector<FuzzDownstreamPtr> streams_;
};

DEFINE_PROTO_FUZZER(const FuzzCase& input) {
  if (harness == nullptr) {
    hcm_config = std::make_unique<FuzzConfig>(Protobuf::RepeatedPtrField<std::string>{});
    harness = std::make_unique<Harness>(*hcm_config.get());
    cluster_manager = std::make_unique<FuzzClusterManager>(*hcm_config.get());
    cluster_manager->createDefaultClusters();
    atexit(cleanup);
  }
  harness->fuzz(input);
  harness->reset();
  fflush(stdout);
}

} // namespace Http
} // namespace Envoy
