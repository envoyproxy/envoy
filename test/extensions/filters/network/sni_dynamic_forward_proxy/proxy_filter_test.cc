#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3/sni_dynamic_forward_proxy.pb.h"
#include "envoy/network/connection.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/filters/network/sni_dynamic_forward_proxy/proxy_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/basic_resource_limit.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/transport_socket_match.h"

using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {
namespace {

using LoadDnsCacheEntryStatus =
    Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;
using MockLoadDnsCacheEntryResult =
    Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult;

class SniDynamicProxyFilterTest
    : public testing::Test,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  void SetUp() override { setupFilter(); }

  virtual void setupFilter() {
    FilterConfig proto_config;
    proto_config.set_port_value(443);
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    absl::Status status = absl::OkStatus();
    filter_config_ = std::make_shared<ProxyFilterConfig>(proto_config, *this, cm_, status);
    EXPECT_TRUE(status.ok());
    filter_ = std::make_unique<ProxyFilter>(filter_config_);
    filter_->initializeReadFilterCallbacks(callbacks_);

    // Allow for an otherwise strict mock.
    ON_CALL(callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    EXPECT_CALL(callbacks_, connection()).Times(AtLeast(0));
  }

  void setFilterStateHost(const std::string& host) {
    connection_.streamInfo().filterState()->setData(
        "envoy.upstream.dynamic_host", std::make_shared<Router::StringAccessorImpl>(host),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  }

  void setFilterStatePort(uint32_t port) {
    connection_.streamInfo().filterState()->setData(
        "envoy.upstream.dynamic_port", std::make_shared<StreamInfo::UInt32AccessorImpl>(port),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  }

  ~SniDynamicProxyFilterTest() override {
    EXPECT_TRUE(
        cm_.thread_local_cluster_.cluster_.info_->resource_manager_->pendingRequests().canCreate());
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  Upstream::MockClusterManager cm_;
  ProxyFilterConfigSharedPtr filter_config_;
  std::unique_ptr<ProxyFilter> filter_;
  Network::MockReadFilterCallbacks callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
};

// No SNI handling.
TEST_F(SniDynamicProxyFilterTest, NoSNI) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCache) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCacheWithHostFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCacheWithPortFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(553);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 553, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCacheWithHostAndPortFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  setFilterStatePort(553);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 553, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCacheWithPort0FromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(0);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsCacheWithPortAboveLimitFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(99999);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(callbacks_, continueReading());
  filter_->onLoadDnsCacheComplete(
      std::make_shared<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>());

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCache) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCacheWithHostFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCacheWithPortFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(553);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 553, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCacheWithHostAndPortFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  setFilterStatePort(553);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 553, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCacheWithPort0FromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(0);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, LoadDnsInCacheWithPortAboveLimitFromFilterState) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  setFilterStatePort(99999);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

// Cache overflow.
TEST_F(SniDynamicProxyFilterTest, CacheOverflow) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt}));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

TEST_F(SniDynamicProxyFilterTest, CircuitBreakerInvoked) {
  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return("foo"));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).WillOnce(Return(nullptr));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

class UpstreamResolvedHostFilterStateHelper : public SniDynamicProxyFilterTest {
public:
  void setupFilter() override {
    FilterConfig proto_config;
    proto_config.set_port_value(443);
    proto_config.set_save_upstream_address(true);

    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    absl::Status status = absl::OkStatus();
    filter_config_ = std::make_shared<ProxyFilterConfig>(proto_config, *this, cm_, status);
    EXPECT_TRUE(status.ok());
    filter_ = std::make_unique<ProxyFilter>(filter_config_);
    filter_->initializeReadFilterCallbacks(callbacks_);

    // Allow for an otherwise strict mock.
    ON_CALL(callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    EXPECT_CALL(callbacks_, connection()).Times(AtLeast(0));
  }
};

// Tests if an already existing address set in filter state is updated when upstream host is
// resolved successfully.
TEST_F(UpstreamResolvedHostFilterStateHelper, UpdateResolvedHostFilterStateMetadata) {
  // Pre-populate the filter state with an address.
  const auto pre_address = Network::Utility::parseInternetAddressNoThrow("1.2.3.3", 443);
  auto address_obj = std::make_unique<StreamInfo::UpstreamAddress>();
  address_obj->address_ = pre_address;
  connection_.streamInfo().filterState()->setData(
      StreamInfo::UpstreamAddress::key(), std::move(address_obj),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 443);

  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*host_info, address()).Times(2).WillRepeatedly(Return(host_info->address_));

  // Host was resolved successfully, so continue filter iteration.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  const StreamInfo::UpstreamAddress* updated_address_obj =
      connection_.streamInfo().filterState()->getDataReadOnly<StreamInfo::UpstreamAddress>(
          StreamInfo::UpstreamAddress::key());

  // Verify the data
  EXPECT_TRUE(updated_address_obj->address_);
  EXPECT_EQ(updated_address_obj->address_->asStringView(), host_info->address_->asStringView());
}

// Tests if address set is populated in the filter state when an upstream host is resolved
// successfully but is null.
TEST_F(UpstreamResolvedHostFilterStateHelper, IgnoreFilterStateMetadataNullAddress) {
  InSequence s;

  // Setup test host
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = nullptr;

  EXPECT_CALL(connection_, requestedServerName()).WillRepeatedly(Return(""));
  setFilterStateHost("foo");
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("foo"), 443, _, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           ProxyFilter::LoadDnsCacheEntryCallbacks&) {
        return MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
      }));

  EXPECT_CALL(*host_info, address());

  // Host was not resolved, still continue filter iteration.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

} // namespace

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
