#include <net/if.h>

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gtest/gtest.h"
#include "library/common/network/connectivity_manager.h"

using testing::_;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Network {

class ConnectivityManagerTest : public testing::Test {
public:
  ConnectivityManagerTest()
      : dns_cache_manager_(
            new NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>()),
        dns_cache_(dns_cache_manager_->dns_cache_),
        connectivity_manager_(std::make_shared<ConnectivityManager>(cm_, dns_cache_manager_)) {
    ON_CALL(*dns_cache_manager_, lookUpCacheByName(_)).WillByDefault(Return(dns_cache_));
    // Toggle network to reset network state.
    ConnectivityManager::setPreferredNetwork(ENVOY_NET_GENERIC);
    ConnectivityManager::setPreferredNetwork(ENVOY_NET_WLAN);
  }

  std::shared_ptr<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>>
      dns_cache_manager_;
  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCache> dns_cache_;
  NiceMock<Upstream::MockClusterManager> cm_{};
  ConnectivityManagerSharedPtr connectivity_manager_;
};

TEST_F(ConnectivityManagerTest, SetPreferredNetworkWithNewNetworkChangesConfigurationKey) {
  envoy_netconf_t original_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t new_key = ConnectivityManager::setPreferredNetwork(ENVOY_NET_WWAN);
  EXPECT_NE(original_key, new_key);
  EXPECT_EQ(new_key, connectivity_manager_->getConfigurationKey());
}

TEST_F(ConnectivityManagerTest,
       DISABLED_SetPreferredNetworkWithUnchangedNetworkReturnsStaleConfigurationKey) {
  envoy_netconf_t original_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t stale_key = ConnectivityManager::setPreferredNetwork(ENVOY_NET_WLAN);
  EXPECT_NE(original_key, stale_key);
  EXPECT_EQ(original_key, connectivity_manager_->getConfigurationKey());
}

TEST_F(ConnectivityManagerTest, RefreshDnsForCurrentConfigurationTriggersDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->refreshDns(configuration_key, false);
}

TEST_F(ConnectivityManagerTest, RefreshDnsForStaleConfigurationDoesntTriggerDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts()).Times(0);
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->refreshDns(configuration_key - 1, false);
}

TEST_F(ConnectivityManagerTest, WhenDrainPostDnsRefreshEnabledDrainsPostDnsRefresh) {
  EXPECT_CALL(*dns_cache_, addUpdateCallbacks_(Ref(*connectivity_manager_)));
  connectivity_manager_->setDrainPostDnsRefreshEnabled(true);

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  EXPECT_CALL(*dns_cache_, iterateHostMap(_))
      .WillOnce(
          Invoke([&](Extensions::Common::DynamicForwardProxy::DnsCache::IterateHostMapCb callback) {
            callback("cached.example.com", host_info);
            callback("cached2.example.com", host_info);
            callback("cached3.example.com", host_info);
          }));

  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->refreshDns(configuration_key, true);

  EXPECT_CALL(cm_, drainConnections(_));
  connectivity_manager_->onDnsResolutionComplete(
      "cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
  connectivity_manager_->onDnsResolutionComplete(
      "not-cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
  connectivity_manager_->onDnsResolutionComplete(
      "not-cached2.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
}

TEST_F(ConnectivityManagerTest, WhenDrainPostDnsNotEnabledDoesntDrainPostDnsRefresh) {
  connectivity_manager_->setDrainPostDnsRefreshEnabled(false);

  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->refreshDns(configuration_key, true);

  EXPECT_CALL(cm_, drainConnections(_)).Times(0);
  connectivity_manager_->onDnsResolutionComplete(
      "example.com", std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
}

TEST_F(ConnectivityManagerTest,
       ReportNetworkUsageDoesntAlterNetworkConfigurationWhenBoundInterfacesAreDisabled) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(false);
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest,
       ReportNetworkUsageTriggersOverrideAfterFirstFaultAfterNetworkUpdate) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisablesOverrideAfterFirstFaultAfterOverride) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  configuration_key = connectivity_manager_->getConfigurationKey();
  EXPECT_EQ(AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisablesOverrideAfterThirdFaultAfterSuccess) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, false /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisregardsCallsWithStaleConfigurationKey) {
  envoy_netconf_t stale_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t current_key = ConnectivityManager::setPreferredNetwork(ENVOY_NET_WWAN);
  EXPECT_NE(stale_key, current_key);

  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);

  EXPECT_EQ(current_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(stale_key, false /* network_fault */);
  connectivity_manager_->reportNetworkUsage(current_key, true /* network_fault */);

  EXPECT_NE(current_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, EnumerateInterfacesFiltersByFlags) {
  // Select loopback.
  auto loopbacks = connectivity_manager_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, 0);
  EXPECT_EQ(loopbacks.size(), 1);
  EXPECT_EQ(std::get<const std::string>(loopbacks[0]).rfind("lo", 0), 0);

  // Reject loopback.
  auto nonloopbacks = connectivity_manager_->enumerateInterfaces(AF_INET, 0, IFF_LOOPBACK);
  for (const auto& interface : nonloopbacks) {
    EXPECT_NE(std::get<const std::string>(interface).rfind("lo", 0), 0);
  }

  // Select AND reject loopback.
  auto empty = connectivity_manager_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, IFF_LOOPBACK);
  EXPECT_EQ(empty.size(), 0);
}

TEST_F(ConnectivityManagerTest, OverridesNoProxySettingsWithNewProxySettings) {
  EXPECT_EQ(nullptr, connectivity_manager_->getProxySettings());

  const auto proxy_settings = ProxySettings::parseHostAndPort("127.0.0.1", 9999);
  connectivity_manager_->setProxySettings(proxy_settings);
  EXPECT_EQ("127.0.0.1:9999", connectivity_manager_->getProxySettings()->asString());
}

TEST_F(ConnectivityManagerTest, OverridesCurrentProxySettingsWithNoProxySettings) {
  const auto proxy_settings = ProxySettings::parseHostAndPort("127.0.0.1", 9999);
  connectivity_manager_->setProxySettings(proxy_settings);
  EXPECT_EQ("127.0.0.1:9999", connectivity_manager_->getProxySettings()->asString());

  connectivity_manager_->setProxySettings(nullptr);
  EXPECT_EQ(nullptr, connectivity_manager_->getProxySettings());
}

TEST_F(ConnectivityManagerTest, OverridesCurrentProxySettingsWithNewProxySettings) {
  const auto proxy_settings1 = ProxySettings::parseHostAndPort("127.0.0.1", 9999);
  connectivity_manager_->setProxySettings(proxy_settings1);
  EXPECT_EQ("127.0.0.1:9999", connectivity_manager_->getProxySettings()->asString());

  const auto proxy_settings2 = ProxySettings::parseHostAndPort("127.0.0.1", 8888);
  connectivity_manager_->setProxySettings(proxy_settings2);
  EXPECT_EQ(proxy_settings2, connectivity_manager_->getProxySettings());
}

TEST_F(ConnectivityManagerTest, IgnoresDuplicatedProxySettingsUpdates) {
  const auto proxy_settings1 = ProxySettings::parseHostAndPort("127.0.0.1", 9999);
  connectivity_manager_->setProxySettings(proxy_settings1);
  EXPECT_EQ("127.0.0.1:9999", connectivity_manager_->getProxySettings()->asString());

  const auto proxy_settings2 = ProxySettings::parseHostAndPort("127.0.0.1", 9999);
  connectivity_manager_->setProxySettings(proxy_settings2);
  EXPECT_EQ(proxy_settings1, connectivity_manager_->getProxySettings());
}

} // namespace Network
} // namespace Envoy
