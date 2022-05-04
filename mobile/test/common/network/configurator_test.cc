#include <net/if.h>

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gtest/gtest.h"
#include "library/common/network/configurator.h"

using testing::_;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Network {

class ConfiguratorTest : public testing::Test {
public:
  ConfiguratorTest()
      : dns_cache_manager_(
            new NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>()),
        dns_cache_(dns_cache_manager_->dns_cache_),
        configurator_(std::make_shared<Configurator>(cm_, dns_cache_manager_)) {
    ON_CALL(*dns_cache_manager_, lookUpCacheByName(_)).WillByDefault(Return(dns_cache_));
    // Toggle network to reset network state.
    Configurator::setPreferredNetwork(ENVOY_NET_GENERIC);
    Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  }

  std::shared_ptr<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>>
      dns_cache_manager_;
  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCache> dns_cache_;
  NiceMock<Upstream::MockClusterManager> cm_{};
  ConfiguratorSharedPtr configurator_;
};

TEST_F(ConfiguratorTest, SetPreferredNetworkWithNewNetworkChangesConfigurationKey) {
  envoy_netconf_t original_key = configurator_->getConfigurationKey();
  envoy_netconf_t new_key = Configurator::setPreferredNetwork(ENVOY_NET_WWAN);
  EXPECT_NE(original_key, new_key);
  EXPECT_EQ(new_key, configurator_->getConfigurationKey());
}

TEST_F(ConfiguratorTest,
       DISABLED_SetPreferredNetworkWithUnchangedNetworkReturnsStaleConfigurationKey) {
  envoy_netconf_t original_key = configurator_->getConfigurationKey();
  envoy_netconf_t stale_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  EXPECT_NE(original_key, stale_key);
  EXPECT_EQ(original_key, configurator_->getConfigurationKey());
}

TEST_F(ConfiguratorTest, RefreshDnsForCurrentConfigurationTriggersDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->refreshDns(configuration_key, false);
}

TEST_F(ConfiguratorTest, RefreshDnsForStaleConfigurationDoesntTriggerDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts()).Times(0);
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->refreshDns(configuration_key - 1, false);
}

TEST_F(ConfiguratorTest, WhenDrainPostDnsRefreshEnabledDrainsPostDnsRefresh) {
  EXPECT_CALL(*dns_cache_, addUpdateCallbacks_(Ref(*configurator_)));
  configurator_->setDrainPostDnsRefreshEnabled(true);

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  EXPECT_CALL(*dns_cache_, iterateHostMap(_))
      .WillOnce(
          Invoke([&](Extensions::Common::DynamicForwardProxy::DnsCache::IterateHostMapCb callback) {
            callback("cached.example.com", host_info);
            callback("cached2.example.com", host_info);
            callback("cached3.example.com", host_info);
          }));

  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->refreshDns(configuration_key, true);

  EXPECT_CALL(cm_, drainConnections(_));
  configurator_->onDnsResolutionComplete(
      "cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
  configurator_->onDnsResolutionComplete(
      "not-cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
  configurator_->onDnsResolutionComplete(
      "not-cached2.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
}

TEST_F(ConfiguratorTest, WhenDrainPostDnsNotEnabledDoesntDrainPostDnsRefresh) {
  configurator_->setDrainPostDnsRefreshEnabled(false);

  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->refreshDns(configuration_key, true);

  EXPECT_CALL(cm_, drainConnections(_)).Times(0);
  configurator_->onDnsResolutionComplete(
      "example.com", std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Success);
}

TEST_F(ConfiguratorTest,
       ReportNetworkUsageDoesntAlterNetworkConfigurationWhenBoundInterfacesAreDisabled) {
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->setInterfaceBindingEnabled(false);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageTriggersOverrideAfterFirstFaultAfterNetworkUpdate) {
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageDisablesOverrideAfterFirstFaultAfterOverride) {
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, configurator_->getConfigurationKey());
  configuration_key = configurator_->getConfigurationKey();
  EXPECT_EQ(AlternateBoundInterfaceMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageDisablesOverrideAfterThirdFaultAfterSuccess) {
  envoy_netconf_t configuration_key = configurator_->getConfigurationKey();
  configurator_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, false /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageDisregardsCallsWithStaleConfigurationKey) {
  envoy_netconf_t stale_key = configurator_->getConfigurationKey();
  envoy_netconf_t current_key = Configurator::setPreferredNetwork(ENVOY_NET_WWAN);
  EXPECT_NE(stale_key, current_key);

  configurator_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(stale_key, true /* network_fault */);
  configurator_->reportNetworkUsage(stale_key, true /* network_fault */);
  configurator_->reportNetworkUsage(stale_key, true /* network_fault */);

  EXPECT_EQ(current_key, configurator_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(stale_key, false /* network_fault */);
  configurator_->reportNetworkUsage(current_key, true /* network_fault */);

  EXPECT_NE(current_key, configurator_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, EnumerateInterfacesFiltersByFlags) {
  // Select loopback.
  auto loopbacks = configurator_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, 0);
  EXPECT_EQ(loopbacks.size(), 1);
  EXPECT_EQ(std::get<const std::string>(loopbacks[0]).rfind("lo", 0), 0);

  // Reject loopback.
  auto nonloopbacks = configurator_->enumerateInterfaces(AF_INET, 0, IFF_LOOPBACK);
  for (const auto& interface : nonloopbacks) {
    EXPECT_NE(std::get<const std::string>(interface).rfind("lo", 0), 0);
  }

  // Select AND reject loopback.
  auto empty = configurator_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, IFF_LOOPBACK);
  EXPECT_EQ(empty.size(), 0);
}

} // namespace Network
} // namespace Envoy
