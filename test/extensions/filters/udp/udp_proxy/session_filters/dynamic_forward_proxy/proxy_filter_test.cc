#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/proxy_filter.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {
namespace {

using LoadDnsCacheEntryStatus =
    Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;
using MockLoadDnsCacheEntryResult =
    Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult;

class DynamicProxyFilterTest
    : public testing::Test,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  DynamicProxyFilterTest() {
    recv_data_stub1_.buffer_ = std::make_unique<Buffer::OwnedImpl>("sometestdata1");
    recv_data_stub2_.buffer_ = std::make_unique<Buffer::OwnedImpl>("sometestdata2");
  }

  void setFilterStateHost(const std::string& host) {
    stream_info_.filterState()->setData(
        "envoy.upstream.dynamic_host", std::make_shared<Envoy::Router::StringAccessorImpl>(host),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  }

  void setFilterStatePort(uint32_t port) {
    stream_info_.filterState()->setData(
        "envoy.upstream.dynamic_port", std::make_shared<StreamInfo::UInt32AccessorImpl>(port),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  }

  void setFilterState(const std::string& host, uint32_t port) {
    setFilterStateHost(host);
    setFilterStatePort(port);
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  void setup(absl::optional<FilterConfig> proto_config = absl::nullopt) {
    FilterConfig config;
    if (proto_config.has_value()) {
      config = proto_config.value();
    }

    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    filter_config_ = std::make_shared<ProxyFilterConfig>(config, *this, server_context_);
    filter_ = std::make_unique<ProxyFilter>(filter_config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
    ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  }

  using MockDnsCacheManager = Extensions::Common::DynamicForwardProxy::MockDnsCacheManager;
  std::shared_ptr<MockDnsCacheManager> dns_cache_manager_ = std::make_shared<MockDnsCacheManager>();
  NiceMock<Server::Configuration::MockFactoryContext> server_context_;
  ProxyFilterConfigSharedPtr filter_config_;
  std::unique_ptr<ProxyFilter> filter_;
  NiceMock<MockReadFilterCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
  Network::UdpRecvData recv_data_stub1_;
  Network::UdpRecvData recv_data_stub2_;
};

TEST_F(DynamicProxyFilterTest, DefaultConfig) {
  setup();
  EXPECT_FALSE(filter_config_->bufferEnabled());
}

TEST_F(DynamicProxyFilterTest, DefaultBufferConfig) {
  FilterConfig config;
  config.mutable_buffer_options();
  setup(config);

  EXPECT_TRUE(filter_config_->bufferEnabled());
  EXPECT_EQ(1024, filter_config_->maxBufferedDatagrams());
  EXPECT_EQ(16384, filter_config_->maxBufferedBytes());
  filter_config_->disableBuffer();
  EXPECT_FALSE(filter_config_->bufferEnabled());
}

TEST_F(DynamicProxyFilterTest, CustomBufferConfig) {
  FilterConfig config;
  auto* buffer_options = config.mutable_buffer_options();
  buffer_options->mutable_max_buffered_datagrams()->set_value(10);
  buffer_options->mutable_max_buffered_bytes()->set_value(20);

  setup(config);
  EXPECT_TRUE(filter_config_->bufferEnabled());
  EXPECT_EQ(10, filter_config_->maxBufferedDatagrams());
  EXPECT_EQ(20, filter_config_->maxBufferedBytes());
}

TEST_F(DynamicProxyFilterTest, StopIterationOnMissingHostAndPort) {
  setup();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnMissingPort) {
  setup();
  setFilterStateHost("host");
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnMissingHost) {
  setup();
  setFilterStatePort(50);
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnEmptyHost) {
  setup();
  setFilterState("", 50);
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnPortIsZero) {
  setup();
  setFilterState("", 0);
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnPortOutOfRange) {
  setup();
  setFilterState("", 65536);
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnRequestOverflow) {
  setup();
  setFilterState("host", 50);
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_()).WillOnce(Return(nullptr));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, StopIterationOnCacheLoadOverflow) {
  setup();
  setFilterState("host", 50);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest, PassesThroughImmediatelyWhenDnsAlreadyInCache) {
  setup();
  setFilterState("host", 50);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(recv_data_stub1_));
}

TEST_F(DynamicProxyFilterTest,
       RequestWithCacheMissShouldStopIterationBufferPopulateCacheAndFlushWhenReady) {
  setup();
  setFilterState("host", 50);
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));

  EXPECT_CALL(callbacks_, continueFilterChain());

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 50);
  EXPECT_CALL(*host_info, address());
  filter_->onLoadDnsCacheComplete(host_info);

  EXPECT_CALL(*handle, onDestroy());
}

TEST_F(DynamicProxyFilterTest, LoadingCacheEntryWithDefaultBufferConfig) {
  FilterConfig config;
  config.mutable_buffer_options();
  setup(config);

  setFilterState("host", 50);
  EXPECT_TRUE(filter_config_->bufferEnabled());
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  // Two datagrams will be buffered.
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub2_));

  EXPECT_CALL(callbacks_, continueFilterChain());
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(2);

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 50);
  EXPECT_CALL(*host_info, address());
  filter_->onLoadDnsCacheComplete(host_info);

  EXPECT_CALL(*handle, onDestroy());
  EXPECT_FALSE(filter_config_->bufferEnabled());
}

TEST_F(DynamicProxyFilterTest, LoadingCacheEntryWithBufferSizeOverflow) {
  FilterConfig config;
  auto* buffer_options = config.mutable_buffer_options();
  buffer_options->mutable_max_buffered_datagrams()->set_value(1);
  setup(config);

  setFilterState("host", 50);
  EXPECT_TRUE(filter_config_->bufferEnabled());
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  // Buffer size is 1, first datagram will be buffered, second will drop.
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub2_));

  EXPECT_CALL(callbacks_, continueFilterChain());
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_));

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 50);
  EXPECT_CALL(*host_info, address());
  filter_->onLoadDnsCacheComplete(host_info);

  EXPECT_CALL(*handle, onDestroy());
  EXPECT_FALSE(filter_config_->bufferEnabled());
}

TEST_F(DynamicProxyFilterTest, LoadingCacheEntryWithBufferBytesOverflow) {
  FilterConfig config;
  auto* buffer_options = config.mutable_buffer_options();
  buffer_options->mutable_max_buffered_bytes()->set_value(strlen("sometestdata1"));
  setup(config);

  setFilterState("host", 50);
  EXPECT_TRUE(filter_config_->bufferEnabled());
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  // Buffer bytes size is 13, first datagram will be buffered, second will drop.
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub2_));

  EXPECT_CALL(callbacks_, continueFilterChain());
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_));

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 50);
  EXPECT_CALL(*host_info, address());
  filter_->onLoadDnsCacheComplete(host_info);

  EXPECT_CALL(*handle, onDestroy());
  EXPECT_FALSE(filter_config_->bufferEnabled());
}

TEST_F(DynamicProxyFilterTest, LoadingCacheEntryWithContinueFilterChainFailure) {
  FilterConfig config;
  config.mutable_buffer_options();
  setup(config);

  setFilterState("host", 50);
  EXPECT_TRUE(filter_config_->bufferEnabled());
  Upstream::ResourceAutoIncDec* circuit_breakers_{
      new Upstream::ResourceAutoIncDec(pending_requests_)};
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, canCreateDnsRequest_())
      .WillOnce(Return(circuit_breakers_));
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("host"), 50, _, _))
      .WillOnce(Return(
          MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt}));
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(recv_data_stub1_));

  // Session is removed and no longer valid, no datagrams will be injected.
  EXPECT_CALL(callbacks_, continueFilterChain()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, injectDatagramToFilterChain(_)).Times(0);
  filter_->onLoadDnsCacheComplete(nullptr);

  EXPECT_CALL(*handle, onDestroy());
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
