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
                                  ServerConnectionCallbacks&, Server::OverloadManager&) override {
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
      : conn_manager_(conn_manager), config_(config) {
    config_.newStream();
    // If sendLocalReply is called:
    ON_CALL(encoder_, encodeHeaders(_, true))
        .WillByDefault(Invoke([this](const ResponseHeaderMap&, bool end_stream) -> void {
          request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
        }));
  }

  void sendHeaders(const test::fuzz::Headers& request_headers, bool end_stream,
                   absl::string_view path) {
    if (request_state_ == StreamState::PendingHeaders) {
      request_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(request_headers, {}, {"host"}));
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
      auto trailers = std::make_unique<TestRequestTrailerMapImpl>(
          Fuzz::fromHeaders<TestRequestTrailerMapImpl>(request_trailers));
      decoder_->decodeTrailers(std::move(trailers));
      request_state_ = StreamState::Closed;
    }
  }

  ConnectionManagerImpl& conn_manager_;
  FuzzConfig& config_;
  RequestDecoder* decoder_{};
  NiceMock<MockResponseEncoder> encoder_;
  StreamState request_state_{StreamState::PendingHeaders};
};
using FuzzDownstreamPtr = std::unique_ptr<FuzzDownstream>;

// This class mocks the upstream and serves as entry point for responses.
class FuzzUpstream {
public:
  FuzzUpstream(Http::ResponseDecoder& decoder) : decoder_(decoder) {
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
  StreamState response_state_{StreamState::PendingHeaders};
  NiceMock<MockRequestEncoder> mock_request_encoder_;
};

// This class mocks an upstream cluster. It holds the set of
// `FuzzUpstream` instances, which result from `FuzzDownstream`
// requests
class FuzzCluster {
public:
  FuzzCluster(FuzzConfig& cfg, const char* path, bool internal_redirect_policy_enabled,
              bool cross_scheme_redirect_allowed, bool allows_early_data_for_request)
      : cfg_(cfg), name_(path), path_(path), mock_route_(new Router::MockRoute()),
        route_(mock_route_), internal_redirect_policy_enabled_(internal_redirect_policy_enabled),
        cross_scheme_redirect_allowed_(cross_scheme_redirect_allowed),
        allows_early_data_for_request_(allows_early_data_for_request) {
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
      auto headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(response_headers, {}, {"status"}));
      uint64_t rc;
      if (!absl::SimpleAtoi(headers->getStatusValue(), &rc)) {
        headers->setStatus(302);
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
      auto trailers = std::make_unique<Http::TestResponseTrailerMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(response_trailers, {}, {"status"}));
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
    route_name_ = route_name;
    direct_response_body_ = body;
    ON_CALL(*direct_response_entry_.get(), responseCode()).WillByDefault(Return(code));
    ON_CALL(*direct_response_entry_.get(), responseBody())
        .WillByDefault(ReturnRef(direct_response_body_));
    ON_CALL(*direct_response_entry_.get(), newUri(_)).WillByDefault(Return(new_uri));
    ON_CALL(*direct_response_entry_.get(), routeName()).WillByDefault(ReturnRef(route_name_));
    ON_CALL(*mock_route_, directResponseEntry())
        .WillByDefault(Return(direct_response_entry_.get()));
  }

  const absl::string_view getPath() const { return absl::string_view(path_); }

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
  const absl::string_view path_;
  Upstream::MockThreadLocalCluster tlc_;
  Router::MockRoute* mock_route_;
  Router::RouteConstSharedPtr route_;
  bool internal_redirect_policy_enabled_;
  bool cross_scheme_redirect_allowed_;
  bool allows_early_data_for_request_;
  bool maintenance_{false};
  StreamInfo::MockStreamInfo mock_stream_info_;

  std::vector<std::unique_ptr<FuzzUpstream>> upstreams_;
  std::unique_ptr<Router::MockDirectResponseEntry> direct_response_entry_{};

  std::string route_name_{};
  std::string direct_response_body_{};
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
    FuzzClusterPtr default0 = std::make_unique<FuzzCluster>(cfg_, "/default0", true, true, true);
    clusters_.push_back(std::move(default0));

    FuzzClusterPtr default1 = std::make_unique<FuzzCluster>(cfg_, "/default1", true, true, true);
    clusters_.push_back(std::move(default1));

    FuzzClusterPtr default2 = std::make_unique<FuzzCluster>(cfg_, "/default2", true, true, true);
    default2->addDirectResponse(Code::Found, "", "/default0", "/default0");
    clusters_.push_back(std::move(default2));
  }

  FuzzCluster& selectOneCluster(uint32_t selection) {
    return *clusters_[selection % clusters_.size()].get();
  }

  FuzzCluster* selectClusterByName(absl::string_view name) {
    for (auto& cluster : clusters_) {
      if (cluster->path_ == name) {
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
    FuzzCluster* cluster = selectClusterByName(path);
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
  Harness(FuzzConfig& cfg) : hcm_config_(cfg), reg_(fuzz_conn_pool_factory_) {
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
        FuzzDownstreamPtr stream = std::make_unique<FuzzDownstream>(*hcm_, hcm_config_);
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

  bool closed_{false};
  std::vector<FuzzDownstreamPtr> streams_;
};

DEFINE_PROTO_FUZZER(const FuzzCase& input) {
  if (harness == nullptr) {
    hcm_config = std::make_unique<FuzzConfig>(Protobuf::RepeatedPtrField<std::string>{});
    harness = std::make_unique<Harness>(*hcm_config);
    cluster_manager = std::make_unique<FuzzClusterManager>(*hcm_config);
    cluster_manager->createDefaultClusters();
    atexit(cleanup);
  }
  harness->fuzz(input);
}

} // namespace Http
} // namespace Envoy
