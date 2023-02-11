#include "source/common/network/address_impl.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/filters/http/network_configuration/filter.h"
#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/network/proxy_settings.h"

using Envoy::Extensions::Common::DynamicForwardProxy::DnsCache;
using Envoy::Extensions::Common::DynamicForwardProxy::MockDnsCache;
using testing::_;
using testing::Eq;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {
namespace {

class MockConnectivityManager : public Network::ConnectivityManager {
public:
  MOCK_METHOD(std::vector<Network::InterfacePair>, enumerateV4Interfaces, ());
  MOCK_METHOD(std::vector<Network::InterfacePair>, enumerateV6Interfaces, ());
  MOCK_METHOD(std::vector<Network::InterfacePair>, enumerateInterfaces,
              (unsigned short family, unsigned int select_flags, unsigned int reject_flags));
  MOCK_METHOD(envoy_network_t, getPreferredNetwork, ());
  MOCK_METHOD(envoy_socket_mode_t, getSocketMode, ());
  MOCK_METHOD(envoy_netconf_t, getConfigurationKey, ());
  MOCK_METHOD(Envoy::Network::ProxySettingsConstSharedPtr, getProxySettings, ());
  MOCK_METHOD(void, reportNetworkUsage, (envoy_netconf_t configuration_key, bool network_fault));
  MOCK_METHOD(void, setProxySettings, (Envoy::Network::ProxySettingsConstSharedPtr proxy_settings));
  MOCK_METHOD(void, setDrainPostDnsRefreshEnabled, (bool enabled));
  MOCK_METHOD(void, setInterfaceBindingEnabled, (bool enabled));
  MOCK_METHOD(void, refreshDns, (envoy_netconf_t configuration_key, bool drain_connections));
  MOCK_METHOD(void, resetConnectivityState, ());
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, getUpstreamSocketOptions,
              (envoy_network_t network, envoy_socket_mode_t socket_mode));
  MOCK_METHOD(envoy_netconf_t, addUpstreamSocketOptions,
              (Network::Socket::OptionsSharedPtr options));

  MOCK_METHOD(void, onDnsHostAddOrUpdate,
              (const std::string& /*host*/,
               const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&));
  MOCK_METHOD(void, onDnsHostRemove, (const std::string& /*host*/));
  MOCK_METHOD(void, onDnsResolutionComplete,
              (const std::string& /*host*/,
               const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&,
               Network::DnsResolver::ResolutionStatus));
  MOCK_METHOD(Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr, dnsCache, ());
};

class NetworkConfigurationFilterTest : public testing::Test {
public:
  NetworkConfigurationFilterTest()
      : connectivity_manager_(new NiceMock<MockConnectivityManager>),
        proxy_settings_(new Network::ProxySettings("127.0.0.1", 82)),
        filter_(connectivity_manager_, false, false) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    ON_CALL(decoder_callbacks_.stream_info_, getRequestHeaders())
        .WillByDefault(Return(&default_request_headers_));
  }

  void createCache() {
    dns_cache_ =
        std::make_shared<NiceMock<Envoy::Extensions::Common::DynamicForwardProxy::MockDnsCache>>();
    ON_CALL(*connectivity_manager_, dnsCache()).WillByDefault(Return(dns_cache_));
    host_info_ = std::make_shared<
        NiceMock<Envoy::Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>();
    address_ = std::make_shared<Network::Address::Ipv4Instance>("224.0.0.1", 0);
    ON_CALL(*host_info_, address()).WillByDefault(Return(address_));
  }

  Network::Address::InstanceConstSharedPtr address_;
  std::shared_ptr<Envoy::Extensions::Common::DynamicForwardProxy::MockDnsCache> dns_cache_;
  std::shared_ptr<Envoy::Extensions::Common::DynamicForwardProxy::MockDnsHostInfo> host_info_;
  std::shared_ptr<MockConnectivityManager> connectivity_manager_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Envoy::Network::ProxySettingsConstSharedPtr proxy_settings_;
  NetworkConfigurationFilter filter_;
  Http::TestRequestHeaderMapImpl default_request_headers_{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "sni.lyft.com"}};
};

TEST_F(NetworkConfigurationFilterTest, NoProxyConfig) {
  // With no proxy config, no proxy info will be added to the stream info.
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.decodeHeaders(default_request_headers_, false));
}

TEST_F(NetworkConfigurationFilterTest, IPProxyConfig) {
  // With an IP based config, expect the proxy information to get added to stream info.
  EXPECT_CALL(*connectivity_manager_, getProxySettings()).WillOnce(Return(proxy_settings_));
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.decodeHeaders(default_request_headers_, false));
}

TEST_F(NetworkConfigurationFilterTest, IPProxyConfigNoAuthority) {
  Http::TestRequestHeaderMapImpl bad_request_headers{{":method", "GET"}};

  // With no authority header, don't even check for proxy settings.
  EXPECT_CALL(*connectivity_manager_, getProxySettings()).Times(0);
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(bad_request_headers, false));
}

TEST_F(NetworkConfigurationFilterTest, HostnameProxyConfigNoCache) {
  proxy_settings_ = std::make_shared<Network::ProxySettings>("localhost", 82),

  // With an hostname based config, and no dns cache, expect a local reply.
      EXPECT_CALL(*connectivity_manager_, getProxySettings()).WillOnce(Return(proxy_settings_));
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState()).Times(0);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(default_request_headers_, false));
}

TEST_F(NetworkConfigurationFilterTest, HostnameProxyConfig) {
  proxy_settings_ = std::make_shared<Network::ProxySettings>("localhost", 82);
  createCache();

  // With an hostname based config, and a cached address, expect the proxy info to be set.
  EXPECT_CALL(*connectivity_manager_, getProxySettings()).WillOnce(Return(proxy_settings_));
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState());
  EXPECT_CALL(*dns_cache_, loadDnsCacheEntry_(Eq("localhost"), 82, false, _))
      .WillOnce(
          Invoke([&](absl::string_view, uint16_t, bool, DnsCache::LoadDnsCacheEntryCallbacks&) {
            return MockDnsCache::MockLoadDnsCacheEntryResult{
                DnsCache::LoadDnsCacheEntryStatus::InCache, nullptr, host_info_};
          }));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.decodeHeaders(default_request_headers_, false));
}

TEST_F(NetworkConfigurationFilterTest, HostnameDnsLookupFail) {
  proxy_settings_ = std::make_shared<Network::ProxySettings>("localhost", 82);
  createCache();

  // With a DNS lookup failure, send a local reply.
  EXPECT_CALL(*connectivity_manager_, getProxySettings()).WillOnce(Return(proxy_settings_));
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState()).Times(0);
  EXPECT_CALL(*dns_cache_, loadDnsCacheEntry_(Eq("localhost"), 82, false, _))
      .WillOnce(Return(MockDnsCache::MockLoadDnsCacheEntryResult{
          DnsCache::LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt}));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(default_request_headers_, false));
}

TEST_F(NetworkConfigurationFilterTest, AsyncDnsLookupSuccess) {
  proxy_settings_ = std::make_shared<Network::ProxySettings>("localhost", 82);
  createCache();

  // With an hostname based config, and a cached address, expect iteration to stop.
  EXPECT_CALL(*connectivity_manager_, getProxySettings()).WillOnce(Return(proxy_settings_));
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState()).Times(0);
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new NiceMock<Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle>();
  EXPECT_CALL(*handle, onDestroy());
  EXPECT_CALL(*dns_cache_, loadDnsCacheEntry_(Eq("localhost"), 82, false, _))
      .WillOnce(
          Invoke([&](absl::string_view, uint16_t, bool, DnsCache::LoadDnsCacheEntryCallbacks&) {
            return MockDnsCache::MockLoadDnsCacheEntryResult{
                DnsCache::LoadDnsCacheEntryStatus::Loading, handle, absl::nullopt};
          }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_.decodeHeaders(default_request_headers_, false));

  // Now complete the resolution. The info should be added to the filter state,
  // and the filter chain should schedule the callback to continue.
  new NiceMock<Event::MockSchedulableCallback>(&decoder_callbacks_.dispatcher_);
  EXPECT_CALL(decoder_callbacks_.stream_info_, filterState());
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_.onLoadDnsCacheComplete(host_info_);
}

} // namespace
} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
