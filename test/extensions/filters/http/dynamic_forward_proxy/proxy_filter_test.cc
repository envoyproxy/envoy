#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/stream_info/address_set_accessor_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/basic_resource_limit.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/transport_socket_match.h"
#include "test/test_common/test_runtime.h"

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
  ProxyFilterTest() {
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    transport_socket_match_ = new NiceMock<Upstream::MockTransportSocketMatcher>(
        Network::TransportSocketFactoryPtr(transport_socket_factory_));
    cm_.thread_local_cluster_.cluster_.info_->transport_socket_matcher_.reset(
        transport_socket_match_);

    envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    filter_config_ = std::make_shared<ProxyFilterConfig>(proto_config, *this, cm_);
    filter_ = std::make_unique<ProxyFilter>(filter_config_);
    filter_->setDecoderFilterCallbacks(callbacks_);

    // Allow for an otherwise strict mock.
    EXPECT_CALL(callbacks_, connection()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_, streamId()).Times(AtLeast(0));

    // Configure upstream cluster to be a Dynamic Forward Proxy since that's the
    // kind we need to do DNS entries for.
    CustomClusterType cluster_type;
    cluster_type.set_name("envoy.clusters.dynamic_forward_proxy");
    cm_.thread_local_cluster_.cluster_.info_->cluster_type_ = cluster_type;

    // Configure max pending to 1 so we can test circuit breaking.
    cm_.thread_local_cluster_.cluster_.info_->resetResourceManager(0, 1, 0, 0, 0);
  }

  ~ProxyFilterTest() override {
    EXPECT_TRUE(
        cm_.thread_local_cluster_.cluster_.info_->resource_manager_->pendingRequests().canCreate());
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  Network::MockTransportSocketFactory* transport_socket_factory_{
      new Network::MockTransportSocketFactory()};
  NiceMock<Upstream::MockTransportSocketMatcher>* transport_socket_match_;
  Upstream::MockClusterManager cm_;
  ProxyFilterConfigSharedPtr filter_config_;
  std::unique_ptr<ProxyFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_{{":authority", "foo"}};
  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
};

// Default port 80 if upstream TLS not configured.
TEST_F(ProxyFilterTest, HttpDefaultPort) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));
  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _))
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _))
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt}));
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, Eq("DNS cache overflow"),
                                         _, _, Eq("DNS cache overflow")));
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Create a second filter for a 2nd request.
  auto filter2 = std::make_unique<ProxyFilter>(filter_config_);
  filter2->setDecoderFilterCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_());
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable,
                                         Eq("Dynamic forward proxy pending request overflow"), _, _,
                                         Eq("Dynamic forward proxy pending request overflow")));
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(true));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Create a second filter for a 2nd request.
  auto filter2 = std::make_unique<ProxyFilter>(filter_config_);
  filter2->setDecoderFilterCallbacks(callbacks_);
  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_());
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::ServiceUnavailable,
                                         Eq("Dynamic forward proxy pending request overflow"), _, _,
                                         Eq("Dynamic forward proxy pending request overflow")));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter2->decodeHeaders(request_headers_, false));

  // Cluster circuit breaker overflow counter won't be incremented.
  EXPECT_EQ(0,
            cm_.thread_local_cluster_.cluster_.info_->stats_.upstream_rq_pending_overflow_.value());
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// No cluster type leads to skipping DNS lookups.
TEST_F(ProxyFilterTest, NoClusterType) {
  cm_.thread_local_cluster_.cluster_.info_->cluster_type_ = absl::nullopt;

  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Cluster that isn't a dynamic forward proxy cluster
TEST_F(ProxyFilterTest, NonDynamicForwardProxy) {
  CustomClusterType cluster_type;
  cluster_type.set_name("envoy.cluster.static");
  cm_.thread_local_cluster_.cluster_.info_->cluster_type_ = cluster_type;

  InSequence s;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*callbacks_.route_,
              mostSpecificPerFilterConfig("envoy.filters.http.dynamic_forward_proxy"))
      .WillOnce(Return(&config));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("bar"), 80, _))
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
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*callbacks_.route_,
              mostSpecificPerFilterConfig("envoy.filters.http.dynamic_forward_proxy"))
      .WillOnce(Return(&config));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("bar:82"), 80, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));

  Http::TestRequestHeaderMapImpl headers{{":authority", "foo"}, {"x-set-header", "bar:82"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(headers, false));

  EXPECT_CALL(*handle, onDestroy());
  filter_->onDestroy();
}

// Tests if address set is populated in the filter state when an upstream host is resolved
// successfully.
TEST_F(ProxyFilterTest, AddResolvedHostFilterStateMetadata) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo());
  auto& filter_state = callbacks_.streamInfo().filterState();

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddress("1.2.3.4", 80);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, getHost(_))
      .WillOnce(
          Invoke([&](absl::string_view)
                     -> absl::optional<const Common::DynamicForwardProxy::DnsHostInfoSharedPtr> {
            return host_info;
          }));

  EXPECT_CALL(*host_info, address());

  EXPECT_CALL(callbacks_, streamInfo());

  // Host was resolved successfully, so continue filter iteration.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // We expect FilterState to be populated
  EXPECT_TRUE(filter_state->hasData<StreamInfo::AddressSetAccessor>(
      StreamInfo::AddressSetAccessorImpl::key()));

  filter_->onDestroy();
}

// Tests if address set is populated in the filter state when an upstream host is resolved
// successfully but is null.
TEST_F(ProxyFilterTest, IgnoreFilterStateMetadataNullAddress) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo());
  auto& filter_state = callbacks_.streamInfo().filterState();

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = nullptr;

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, getHost(_))
      .WillOnce(
          Invoke([&](absl::string_view)
                     -> absl::optional<const Common::DynamicForwardProxy::DnsHostInfoSharedPtr> {
            return host_info;
          }));

  EXPECT_CALL(*host_info, address());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // We do not expect FilterState to be populated
  EXPECT_FALSE(filter_state->hasData<StreamInfo::AddressSetAccessor>(
      StreamInfo::AddressSetAccessorImpl::key()));

  filter_->onDestroy();
}

// Tests if an already existing address set in filter state is updated when upstream host is
// resolved successfully.
TEST_F(ProxyFilterTest, UpdateResolvedHostFilterStateMetadata) {
  Upstream::ResourceAutoIncDec* circuit_breakers_(
      new Upstream::ResourceAutoIncDec(pending_requests_));

  EXPECT_CALL(callbacks_, streamInfo());

  // Pre-populate the filter state with an address.
  auto& filter_state = callbacks_.streamInfo().filterState();
  const auto pre_address = Network::Utility::parseInternetAddress("1.2.3.3", 80);
  auto address_set = std::make_unique<StreamInfo::AddressSetAccessorImpl>();
  address_set->add(pre_address);
  filter_state->setData(StreamInfo::AddressSetAccessorImpl::key(), std::move(address_set),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::Request);

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddress("1.2.3.4", 80);

  EXPECT_CALL(callbacks_, route());
  EXPECT_CALL(cm_, getThreadLocalCluster(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*transport_socket_factory_, implementsSecureTransport()).WillOnce(Return(false));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 80, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*dns_cache_manager_->dns_cache_, getHost(_))
      .WillOnce(
          Invoke([&](absl::string_view)
                     -> absl::optional<const Common::DynamicForwardProxy::DnsHostInfoSharedPtr> {
            return host_info;
          }));

  EXPECT_CALL(*host_info, address());

  EXPECT_CALL(callbacks_, streamInfo());

  // Host was resolved successfully, so continue filter iteration.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // We expect FilterState to be populated
  EXPECT_TRUE(filter_state->hasData<StreamInfo::AddressSetAccessor>(
      StreamInfo::AddressSetAccessorImpl::key()));

  // Make sure filter state has pre and new addresses.
  const StreamInfo::AddressSetAccessor& updated_address_set =
      filter_state->getDataReadOnly<StreamInfo::AddressSetAccessor>(
          StreamInfo::AddressSetAccessorImpl::key());

  absl::flat_hash_set<absl::string_view> populated_addresses;
  updated_address_set.iterate([&](const Network::Address::InstanceConstSharedPtr& address) {
    populated_addresses.insert(address->asStringView());
    return true;
  });

  // Verify the data
  EXPECT_EQ(populated_addresses.size(), 1);
  EXPECT_FALSE(populated_addresses.contains(pre_address->asStringView()));
  EXPECT_TRUE(populated_addresses.contains(host_info->address_->asStringView()));

  filter_->onDestroy();
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
