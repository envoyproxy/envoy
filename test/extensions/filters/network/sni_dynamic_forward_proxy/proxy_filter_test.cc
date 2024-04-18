#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3/sni_dynamic_forward_proxy.pb.h"
#include "envoy/network/connection.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/extensions/filters/network/sni_dynamic_forward_proxy/proxy_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/basic_resource_limit.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/transport_socket_match.h"

using testing::AtLeast;
using testing::Eq;
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
  SniDynamicProxyFilterTest() {
    FilterConfig proto_config;
    proto_config.set_port_value(443);
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    filter_config_ = std::make_shared<ProxyFilterConfig>(proto_config, *this, cm_);
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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>());

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

} // namespace

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
