#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/basic_resource_limit.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/transport_socket_match.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {
namespace {

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;
using MockLoadDnsCacheEntryResult =
    Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult;

class ProxyFilterTest : public testing::Test,
                        public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  void SetUp() override {
    setupSocketMatcher();
    setupFilter();
    setupCluster();
  }

  void setupSocketMatcher() {
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    transport_socket_match_ = new NiceMock<Upstream::MockTransportSocketMatcher>(
        Network::UpstreamTransportSocketFactoryPtr(transport_socket_factory_));
    factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
        ->transport_socket_matcher_.reset(transport_socket_match_);
    dfp_cluster_ =
        std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDfpCluster>>();
    auto cluster = std::dynamic_pointer_cast<Extensions::Common::DynamicForwardProxy::DfpCluster>(
        dfp_cluster_);
    Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
        *factory_context_.server_factory_context_.singleton_manager_);
    cluster_store_factory.get()->save("fake_cluster", cluster);
  }

  virtual void setupFilter() {
    EXPECT_CALL(*dns_cache_manager_, getCache(_));

    Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
        *factory_context_.server_factory_context_.singleton_manager_);
    envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
    filter_config_ = std::make_shared<ProxyFilterConfig>(
        proto_config, dns_cache_manager_->getCache(proto_config.dns_cache_config()).value(),
        this->get(), cluster_store_factory, factory_context_);
    filter_ = std::make_unique<ProxyFilter>(filter_config_);

    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  void setupCluster() {
    // Allow for an otherwise strict mock.
    EXPECT_CALL(callbacks_, connection()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_, streamId()).Times(AtLeast(0));

    // Configure upstream cluster to be a Dynamic Forward Proxy since that's the
    // kind we need to do DNS entries for.
    CustomClusterType cluster_type;
    cluster_type.set_name("envoy.clusters.dynamic_forward_proxy");
    factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
        ->cluster_type_ =
        std::make_unique<const envoy::config::cluster::v3::Cluster::CustomClusterType>(
            cluster_type);

    // Configure max pending to 1 so we can test circuit breaking.
    factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
        ->resetResourceManager(0, 1, 0, 0, 0, 100);
  }

  ~ProxyFilterTest() override {
    EXPECT_TRUE(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->resource_manager_->pendingRequests()
                    .canCreate());
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  Network::MockTransportSocketFactory* transport_socket_factory_{
      new Network::MockTransportSocketFactory()};
  NiceMock<Upstream::MockTransportSocketMatcher>* transport_socket_match_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::shared_ptr<NiceMock<Extensions::Common::DynamicForwardProxy::MockDfpCluster>> dfp_cluster_;
  ProxyFilterConfigSharedPtr filter_config_;
  std::unique_ptr<ProxyFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_{{":authority", "foo"}};
  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
};

// Default port 80 if upstream TLS not configured.
TEST_F(ProxyFilterTest, HttpDefaultPort) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());

  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// Default port 443 if upstream TLS is configured.
TEST_F(ProxyFilterTest, HttpsDefaultPort) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());

  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// Cache overflow.
TEST_F(ProxyFilterTest, CacheOverflow) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt}));
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, Eq("DNS cache overflow"),
                                         _, _, Eq("dns_cache_overflow")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

// Circuit breaker overflow
TEST_F(ProxyFilterTest, CircuitBreakerOverflow) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Create a second filter for a 2nd request.
  auto filter2 = std::make_unique<ProxyFilter>(filter_config_);
  filter2->setDecoderFilterCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_());
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable,
                                         Eq("Dynamic forward proxy pending request overflow"), _, _,
                                         Eq("dynamic_forward_proxy_pending_request_overflow")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter2->decodeHeaders(request_headers_, false));

  filter2->onDestroy();
  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// Circuit breaker overflow with DNS Cache resource manager
TEST_F(ProxyFilterTest, CircuitBreakerOverflowWithDnsCacheResourceManager) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Create a second filter for a 2nd request.
  auto filter2 = std::make_unique<ProxyFilter>(filter_config_);
  filter2->setDecoderFilterCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_());
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable,
                                         Eq("Dynamic forward proxy pending request overflow"), _, _,
                                         Eq("dynamic_forward_proxy_pending_request_overflow")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter2->decodeHeaders(request_headers_, false));

  // Cluster circuit breaker overflow counter won't be incremented.
  EXPECT_EQ(0, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->trafficStats()
                   ->upstream_rq_pending_overflow_.value());
  filter2->onDestroy();
  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// No route handling.
TEST_F(ProxyFilterTest, NoRoute) {
  InSequence s;

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// No cluster handling.
TEST_F(ProxyFilterTest, NoCluster) {
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// No cluster type leads to skipping DNS lookups.
TEST_F(ProxyFilterTest, NoClusterType) {
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->cluster_type_ = nullptr;

  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Cluster that isn't a dynamic forward proxy cluster
TEST_F(ProxyFilterTest, NonDynamicForwardProxy) {
  CustomClusterType cluster_type;
  cluster_type.set_name("envoy.cluster.static");
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->cluster_type_ =
      std::make_unique<const envoy::config::cluster::v3::Cluster::CustomClusterType>(cluster_type);

  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(ProxyFilterTest, HostRewrite) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig proto_config;
  proto_config.set_host_rewrite_literal("bar");
  ProxyPerRouteConfig config(proto_config);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_)).WillOnce(Return(&config));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("bar"), 80, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

TEST_F(ProxyFilterTest, HostRewriteViaHeader) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig proto_config;
  proto_config.set_host_rewrite_header("x-set-header");
  ProxyPerRouteConfig config(proto_config);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_)).WillOnce(Return(&config));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("bar:82"), 80, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));

  Http::TestRequestHeaderMapImpl headers{{":authority", "foo"}, {"x-set-header", "bar:82"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(headers, false));

  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// Thread local cluster not exists.
TEST_F(ProxyFilterTest, SubClusterNotExists) {
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*(dfp_cluster_.get()), enableSubCluster()).WillOnce(Return(true));
  // get DFPCluster, not exists.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("DFPCluster:foo:80")));
  // "true" means another thread already created it.
  EXPECT_CALL(*(dfp_cluster_.get()), createSubClusterConfig(_, _, _))
      .WillOnce(Return(std::make_pair(true, absl::nullopt)));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

// Thread local cluster exists.
TEST_F(ProxyFilterTest, SubClusterExists) {
  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"DFPCluster:foo:80"});
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*(dfp_cluster_.get()), enableSubCluster()).WillOnce(Return(true));
  // get DFPCluster, exists.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("DFPCluster:foo:80")));
  EXPECT_CALL(*(dfp_cluster_.get()), touch(_)).WillOnce(Return(true));
  // should not create.
  EXPECT_CALL(*(dfp_cluster_.get()), createSubClusterConfig(_, _, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

// Sub cluster overflow.
TEST_F(ProxyFilterTest, SubClusterOverflow) {
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*(dfp_cluster_.get()), enableSubCluster()).WillOnce(Return(true));
  // get DFPCluster
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("DFPCluster:foo:80")));
  // reach the max_sub_clusters limitation.
  EXPECT_CALL(*(dfp_cluster_.get()), createSubClusterConfig(_, _, _))
      .WillOnce(Return(std::make_pair(false, absl::nullopt)));

  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, Eq("Sub cluster overflow"),
                                         _, _, Eq("sub_cluster_overflow")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

// DFP cluster is removed early.
TEST_F(ProxyFilterTest, DFPClusterIsGone) {
  Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
      *factory_context_.server_factory_context_.singleton_manager_);
  cluster_store_factory.get()->remove("fake_cluster");
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*(dfp_cluster_.get()), enableSubCluster()).Times(0);
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable,
                                         Eq("Dynamic forward proxy cluster is gone"), _, _,
                                         Eq("dynamic_forward_proxy_cluster_is_gone")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

// Sub cluster init timeout
TEST_F(ProxyFilterTest, SubClusterInitTimeout) {
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*(dfp_cluster_.get()), enableSubCluster()).WillOnce(Return(true));
  // get DFPCluster, not exists.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("DFPCluster:foo:80")));
  // "true" means another thread already created it.
  EXPECT_CALL(*(dfp_cluster_.get()), createSubClusterConfig(_, _, _))
      .WillOnce(Return(std::make_pair(true, absl::nullopt)));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, Eq("Sub cluster warming timeout"), _,
                             _, Eq("sub_cluster_warming_timeout")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));

  filter_->onClusterInitTimeout();
  filter_->onDestroy();
}

class UpstreamResolvedHostFilterStateHelper : public ProxyFilterTest {
public:
  void setupFilter() override {
    EXPECT_CALL(*dns_cache_manager_, getCache(_));

    envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
    proto_config.set_save_upstream_address(true);

    Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
        factory_context_.serverFactoryContext().singletonManager());

    filter_config_ = std::make_shared<ProxyFilterConfig>(
        proto_config, dns_cache_manager_->getCache(proto_config.dns_cache_config()).value(),
        this->get(), cluster_store_factory, factory_context_);
    filter_ = std::make_unique<ProxyFilter>(filter_config_);

    filter_->setDecoderFilterCallbacks(callbacks_);
  }
};

// Tests if address set is populated in the filter state when an upstream host is resolved
// successfully.
TEST_F(UpstreamResolvedHostFilterStateHelper, AddResolvedHostFilterStateMetadata) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo()).Times(AnyNumber());
  EXPECT_CALL(callbacks_, dispatcher()).Times(AnyNumber());
  auto& filter_state = callbacks_.streamInfo().filterState();

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 80);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*host_info, address()).Times(2).WillRepeatedly(Return(host_info->address_));

  // Host was resolved successfully, so continue filter iteration.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // We expect FilterState to be populated
  EXPECT_TRUE(
      filter_state->hasData<StreamInfo::UpstreamAddress>(StreamInfo::UpstreamAddress::key()));

  filter_->onDestroy();
}

// Tests if an already existing address set in filter state is updated when upstream host is
// resolved successfully.
TEST_F(UpstreamResolvedHostFilterStateHelper, UpdateResolvedHostFilterStateMetadata) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo()).Times(AnyNumber());
  EXPECT_CALL(callbacks_, dispatcher()).Times(AnyNumber());

  // Pre-populate the filter state with an address.
  auto& filter_state = callbacks_.streamInfo().filterState();
  const auto pre_address = Network::Utility::parseInternetAddressNoThrow("1.2.3.3", 80);
  auto address_obj = std::make_unique<StreamInfo::UpstreamAddress>();
  address_obj->address_ = pre_address;
  filter_state->setData(StreamInfo::UpstreamAddress::key(), std::move(address_obj),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::Request);

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 80);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(callbacks_, streamInfo());
  EXPECT_CALL(callbacks_, dispatcher());

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*host_info, address()).Times(2).WillRepeatedly(Return(host_info->address_));

  // Host was resolved successfully, so continue filter iteration.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // We expect FilterState and resolution times to be populated
  EXPECT_TRUE(
      callbacks_.streamInfo().downstreamTiming().getValue(ProxyFilter::DNS_START).has_value());
  EXPECT_TRUE(
      callbacks_.streamInfo().downstreamTiming().getValue(ProxyFilter::DNS_END).has_value());

  const StreamInfo::UpstreamAddress* updated_address_obj =
      filter_state->getDataReadOnly<StreamInfo::UpstreamAddress>(
          StreamInfo::UpstreamAddress::key());

  // Verify the data
  EXPECT_TRUE(updated_address_obj->address_);
  EXPECT_EQ(updated_address_obj->address_->asStringView(), host_info->address_->asStringView());

  filter_->onDestroy();
}

// Tests if address set is populated in the filter state when an upstream host is resolved
// successfully but is null.
TEST_F(UpstreamResolvedHostFilterStateHelper, IgnoreFilterStateMetadataNullAddress) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo()).Times(AnyNumber());
  EXPECT_CALL(callbacks_, dispatcher()).Times(AnyNumber());
  InSequence s;

  // Setup test host
  auto host_info =
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>();
  host_info->address_ = nullptr;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*host_info, address());
  EXPECT_CALL(callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, Eq("DNS resolution failure"), _, _,
                             Eq("dns_resolution_failure{}")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  filter_->onDestroy();
}

class MockProxyFilter : public ProxyFilter {
public:
  MockProxyFilter(const ProxyFilterConfigSharedPtr& config) : ProxyFilter(config) {}
  ~MockProxyFilter() override = default;
  MOCK_METHOD(bool, isProxying, ());
};

class ProxySettingsProxyFilterTest : public ProxyFilterTest {
public:
  virtual void setupFilter() override {
    EXPECT_CALL(*dns_cache_manager_, getCache(_));

    Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
        *factory_context_.server_factory_context_.singleton_manager_);
    envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
    filter_config_ = std::make_shared<ProxyFilterConfig>(
        proto_config, dns_cache_manager_->getCache(proto_config.dns_cache_config()).value(),
        this->get(), cluster_store_factory, factory_context_);
    mock_filter_ = std::make_unique<NiceMock<MockProxyFilter>>(filter_config_);
    // Set it up such that the filter has proxy settings enabled.
    ON_CALL(*mock_filter_, isProxying()).WillByDefault(Return(true));
    mock_filter_->setDecoderFilterCallbacks(callbacks_);
  }

protected:
  std::unique_ptr<NiceMock<MockProxyFilter>> mock_filter_;
};

TEST_F(ProxySettingsProxyFilterTest, HttpWithProxySettings) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillRepeatedly(Return(circuit_breakers_));

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(_, _, _, _))
      .WillRepeatedly(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info}));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            mock_filter_->decodeHeaders(request_headers_, false));

  mock_filter_->onDestroy();
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
