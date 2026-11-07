#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/router/router.h"
#include "source/common/router/upstream_codec_filter.h"

#include "test/common/http/common.h"
#include "test/integration/filters/test_filters.pb.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
namespace Envoy {
namespace Router {
namespace {

using envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class TestFilter : public Filter {
public:
  using Filter::Filter;

  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&,
                                 Server::Configuration::CommonFactoryContext&, Event::Dispatcher&,
                                 Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
};

// An upstream HTTP filter that tracks its own lifecycle. It is used to detect the router
// driving decode callbacks on a filter that has already been destroyed.
class LifecycleTrackingFilter : public Http::StreamDecoderFilter {
public:
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    ++decode_headers_;
    EXPECT_FALSE(destroyed_);
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ++decode_data_;
    EXPECT_FALSE(destroyed_);
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    ++decode_trailers_;
    EXPECT_FALSE(destroyed_);
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }
  void onDestroy() override { destroyed_ = true; }

  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool destroyed_{false};
  int decode_headers_{0};
  int decode_data_{0};
  int decode_trailers_{0};
};

// Records what the router hands to the factory of an upstream HTTP filter.
class StatsRecordingFilterFactory : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    stats_prefix_ = extra_context.stats_prefix;
    scope_ = &extra_context.scopeOr(context);
    stats_prefix_scope_ = &extra_context.statsPrefixScopeOr(context);
    stats_prefix_or_ = extra_context.statsPrefixOr();
    return [](Http::FilterChainFactoryCallbacks&) {};
  }
  bool isUnifiedFilter() override { return true; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::PassthroughFilterConfig>();
  }
  std::string name() const override { return "stats-recording-filter"; }

  std::string stats_prefix_;
  Stats::Scope* scope_{};
  Stats::Scope* stats_prefix_scope_{};
  std::string stats_prefix_or_;
};

class RouterUpstreamFilterTest : public testing::Test {
public:
  void init(std::vector<HttpFilter> upstream_filters, const std::string& stats_prefix = "prefix.",
            OptRef<Stats::Scope> stats_prefix_scope = {}) {
    envoy::extensions::filters::http::router::v3::Router router_proto;
    static const std::string cluster_name = "cluster_0";

    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    ON_CALL(*cluster_info_, name()).WillByDefault(ReturnRef(cluster_name));
    ON_CALL(*cluster_info_, observabilityName()).WillByDefault(ReturnRef(cluster_name));
    callbacks_.stream_info_.upstream_cluster_info_ = cluster_info_;
    EXPECT_CALL(callbacks_.dispatcher_, deferredDelete_).Times(testing::AnyNumber());
    for (const auto& filter : upstream_filters) {
      *router_proto.add_upstream_http_filters() = filter;
    }

    auto extra_context = Server::Configuration::ExtraFactoryContext::create(context_, stats_prefix);
    extra_context.stats_prefix_scope = stats_prefix_scope;
    config_ = *FilterConfig::create(context_.server_factory_context_, extra_context,
                                    ShadowWriterPtr(new MockShadowWriter()), router_proto);
    router_ = std::make_shared<TestFilter>(config_, config_->default_stats_);
    router_->setDecoderFilterCallbacks(callbacks_);
    EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_)).Times(testing::AnyNumber());
    EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_)).Times(testing::AnyNumber());

    upstream_locality_.set_zone("to_az");
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    ON_CALL(
        *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
        address())
        .WillByDefault(Return(host_address_));
    ON_CALL(
        *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
        locality())
        .WillByDefault(ReturnRef(upstream_locality_));
    ON_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_, chooseHost(_))
        .WillByDefault([this] {
          return Upstream::HostSelectionResponse{
              context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_};
        });
    router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
        ->setLocalAddress(host_address_);
    router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
        ->setRemoteAddress(Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:80"));
  }

  Http::TestRequestHeaderMapImpl run() {
    NiceMock<Http::MockRequestEncoder> encoder;
    Http::ResponseDecoder* response_decoder = nullptr;

    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_,
                newStream(_, _, _))
        .WillOnce([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                      const Http::ConnectionPool::Instance::StreamOptions&)
                      -> Http::ConnectionPool::Cancellable* {
          response_decoder = &decoder;
          EXPECT_CALL(encoder.stream_, connectionInfoProvider())
              .WillRepeatedly(ReturnRef(connection_info1_));
          callbacks.onPoolReady(encoder,
                                context_.server_factory_context_.cluster_manager_
                                    .thread_local_cluster_.conn_pool_.host_,
                                stream_info_, Http::Protocol::Http10);
          return nullptr;
        });

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    EXPECT_CALL(*router_->retry_state_, shouldRetryHeaders(_, _, _))
        .WillOnce(Return(RetryStatus::No));

    Http::ResponseHeaderMapPtr response_headers(new Http::TestResponseHeaderMapImpl());
    response_headers->setStatus(200);

    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_
                    .host_->outlier_detector_,
                putResult(_, std::optional<uint64_t>(200)));
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    response_decoder->decodeHeaders(std::move(response_headers), true);
    return headers;
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::config::core::v3::Locality upstream_locality_;
  Network::Address::InstanceConstSharedPtr host_address_{
      *Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  Network::Address::InstanceConstSharedPtr upstream_local_address1_{
      *Network::Utility::resolveUrl("tcp://10.0.0.5:10211")};
  Network::ConnectionInfoSetterImpl connection_info1_{upstream_local_address1_,
                                                      upstream_local_address1_};

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<TestFilter> router_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RouterUpstreamFilterTest, UpstreamFilter) {
  HttpFilter add_header_filter;
  add_header_filter.set_name("add-header-filter");
  test::integration::filters::AddHeaderEmptyFilterConfig add_header_config;
  std::ignore = add_header_filter.mutable_typed_config()->PackFrom(add_header_config);

  HttpFilter codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec upstream_codec_config;
  std::ignore = codec_filter.mutable_typed_config()->PackFrom(upstream_codec_config);

  init({add_header_filter, codec_filter});
  auto headers = run();
  EXPECT_FALSE(headers.get(Http::LowerCaseString("x-header-to-add")).empty());
}

// Regression test for decode-after-destroy of the upstream HTTP filter chain.
//
// A circuit breaker overflow surfacing synchronously inside
// ConnPoolImplBase::newStream() (pending requests overflow, connection limit
// load shedding, or max requests overflow when a ready client exists) resets
// the upstream request and deferred-deletes it before newStream() returns.
// Deferred deletion runs UpstreamRequest::deleteIsPending() -> cleanUp() ->
// destroyFilters() synchronously, so every upstream HTTP filter has already
// seen onDestroy() when UpstreamRequest::acceptHeadersFromRouter() resumes and
// drives decodeHeaders() on the destroyed filter chain. Native C++ filters
// tolerate decode-after-destroy; the dynamic modules HTTP filter does not: it
// releases its in-module filter in onDestroy() and dereferences stale state
// when decode headers run afterwards. acceptHeadersFromRouter() must not
// continue filter chain processing when the request was reset inside
// newStream().
TEST_F(RouterUpstreamFilterTest, SyncPoolFailureDoesNotDecodeAfterDestroy) {
  auto filter = std::make_shared<LifecycleTrackingFilter>();

  HttpFilter codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec upstream_codec_config;
  std::ignore = codec_filter.mutable_typed_config()->PackFrom(upstream_codec_config);

  init({codec_filter});

  // The upstream request resolves the cluster through the thread-local cluster, so point its
  // cluster info at our instrumented mock.
  ON_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_, info())
      .WillByDefault(Return(cluster_info_));

  // Install the tracking filter ahead of the codec filter via the cluster's filter chain
  // factory, which takes precedence over the router-specified chain.
  EXPECT_CALL(*cluster_info_, createFilterChain(_))
      .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamDecoderFilter(filter);
        // Fall through to the router-specified chain, which provides the codec filter.
        return false;
      });

  // Fail the pool synchronously inside newStream(), the way pending requests circuit
  // breaker overflow does in ConnPoolImplBase::newStreamImpl().
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_,
              newStream(_, _, _))
      .WillOnce([&](Http::ResponseDecoder&, Http::ConnectionPool::Callbacks& callbacks,
                    const Http::ConnectionPool::Instance::StreamOptions&)
                    -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(Http::ConnectionPool::PoolFailureReason::Overflow,
                                "max pending streams overflow",
                                context_.server_factory_context_.cluster_manager_
                                    .thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      });

  // The request should shed cleanly with a 503.
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  // The synchronous overflow reset the upstream request, which ran cleanUp() and
  // onDestroy() on the upstream HTTP filters before newStream() returned.
  EXPECT_TRUE(filter->destroyed_);
  // The destroyed filter chain must not be driven any further.
  EXPECT_EQ(0, filter->decode_headers_);
  EXPECT_EQ(0, filter->decode_data_);
  EXPECT_EQ(0, filter->decode_trailers_);
}

// Regression test for a use-after-free at teardown. A dynamic (ECDS) upstream HTTP filter causes
// FilterConfig to create a Filter::FilterConfigSubscription, which holds a raw reference to the
// filter config provider manager and dereferences it from its destructor. That manager is an
// *unpinned* singleton, so it survives only as long as something holds a strong reference to it:
// FilterConfig must be that holder for the providers it owns.
TEST_F(RouterUpstreamFilterTest, DynamicFilterKeepsConfigProviderManagerAlive) {
  HttpFilter dynamic_filter;
  dynamic_filter.set_name("dynamic-filter");
  auto* config_discovery = dynamic_filter.mutable_config_discovery();
  config_discovery->mutable_config_source()->mutable_ads();
  config_discovery->add_type_urls(
      "type.googleapis.com/test.integration.filters.AddHeaderEmptyFilterConfig");

  HttpFilter codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec upstream_codec_config;
  std::ignore = codec_filter.mutable_typed_config()->PackFrom(upstream_codec_config);

  init({dynamic_filter, codec_filter});
  ASSERT_NE(nullptr, config_);

  // Looking the singleton up again returns the live instance if one exists and creates a fresh one
  // otherwise. Binding it to a weak_ptr and letting the returned strong reference expire at the end
  // of the statement therefore reports whether `config_` is keeping the manager alive: without that
  // reference, the manager created during init() is already gone and every subscription it owns is
  // left holding a dangling reference.
  std::weak_ptr<Http::UpstreamFilterConfigProviderManager> manager =
      Http::FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
          context_.server_factory_context_);
  EXPECT_FALSE(manager.expired());

  // Destroying the router config destroys the providers and their subscription, which writes
  // through that reference. The manager must still be alive at that point (this is where ASAN
  // reports the use-after-free without the fix) and may be released only afterwards.
  router_.reset();
  config_.reset();
  EXPECT_TRUE(manager.expired());
}

class RouterUpstreamFilterStatsTest : public RouterUpstreamFilterTest {
public:
  void initWithRecordingFilter(const std::string& stats_prefix,
                               OptRef<Stats::Scope> stats_prefix_scope = {}) {
    HttpFilter recording_filter;
    recording_filter.set_name("stats-recording-filter");
    std::ignore = recording_filter.mutable_typed_config()->PackFrom(
        test::integration::filters::PassthroughFilterConfig());

    HttpFilter codec_filter;
    codec_filter.set_name("envoy.filters.http.upstream_codec");
    envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec upstream_codec_config;
    std::ignore = codec_filter.mutable_typed_config()->PackFrom(upstream_codec_config);

    init({recording_filter, codec_filter}, stats_prefix, stats_prefix_scope);
  }

  StatsRecordingFilterFactory factory_;
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration_{
      factory_};
};

TEST_F(RouterUpstreamFilterStatsTest, NoStatsPrefixScope) {
  initWithRecordingFilter("http.ingress.");

  EXPECT_EQ("http.ingress.rq_total", config_->default_stats_.rq_total_.name());
  // The prefix is passed as is rather than re-decoded from a stat name, which drops the trailing
  // dot.
  EXPECT_EQ("http.ingress.", factory_.stats_prefix_);
  EXPECT_EQ(&context_.scope(), factory_.scope_);
  EXPECT_EQ(&context_.scope(), factory_.stats_prefix_scope_);
  EXPECT_EQ("http.ingress.", factory_.stats_prefix_or_);
}

TEST_F(RouterUpstreamFilterStatsTest, StatsPrefixScope) {
  // Named differently from the prefix to tell which one the stats got their name from.
  Stats::ScopeSharedPtr prefixed_scope = context_.scope().createScope("prefixed.");
  initWithRecordingFilter("http.ingress.", *prefixed_scope);

  EXPECT_EQ("prefixed.rq_total", config_->default_stats_.rq_total_.name());
  // The vhost, vcluster and route level stats stay in the root scope.
  EXPECT_EQ(&context_.scope(), &config_->scope_);

  EXPECT_EQ("http.ingress.", factory_.stats_prefix_);
  EXPECT_EQ(&context_.scope(), factory_.scope_);
  EXPECT_EQ(prefixed_scope.get(), factory_.stats_prefix_scope_);
  EXPECT_EQ("", factory_.stats_prefix_or_);
}

// TODO(wbpcode): remove this test once the
// envoy.reloadable_features.ecds_filter_use_parent_stats_prefix runtime flag is removed.
TEST_F(RouterUpstreamFilterStatsTest, StatsPrefixScopeNotGivenToOtherPrefix) {
  Stats::ScopeSharedPtr prefixed_scope = context_.scope().createScope("prefixed.");
  initWithRecordingFilter("http.ingress.", *prefixed_scope);

  // E.g. an ECDS filter that is created with the prefix of its subscription.
  const std::string other_prefix = "extension_config_discovery.upstream_http_filter.foo.";
  auto extra_context =
      Server::Configuration::ExtraFactoryContext::create(*config_->upstream_ctx_, other_prefix);
  EXPECT_FALSE(extra_context.stats_prefix_scope.has_value());
  EXPECT_EQ(other_prefix, extra_context.statsPrefixOr());
}

TEST_F(RouterUpstreamFilterStatsTest, StatsPrefixScopeWithLegacyEmptyPrefix) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.upstream_http_filters_correct_stats_prefix", "false"}});
  Stats::ScopeSharedPtr prefixed_scope = context_.scope().createScope("prefixed.");
  initWithRecordingFilter("http.ingress.", *prefixed_scope);

  EXPECT_EQ("", factory_.stats_prefix_);
  EXPECT_EQ(&context_.scope(), factory_.stats_prefix_scope_);
  EXPECT_EQ("", factory_.stats_prefix_or_);
}

} // namespace
} // namespace Router
} // namespace Envoy
