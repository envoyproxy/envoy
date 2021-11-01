#include <net/if.h>

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"

#include "gtest/gtest.h"
#include "library/common/network/configurator.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Network {

class ConfiguratorTest : public testing::Test {
public:
  ConfiguratorTest()
      : dns_cache_manager_(
            new NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>()),
        dns_cache_(dns_cache_manager_->dns_cache_),
        configurator_(std::make_shared<Configurator>(dns_cache_manager_)) {
    ON_CALL(*dns_cache_manager_, lookUpCacheByName(_)).WillByDefault(Return(dns_cache_));
  }

  std::shared_ptr<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>>
      dns_cache_manager_;
  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCache> dns_cache_;
  ConfiguratorSharedPtr configurator_;
};

TEST_F(ConfiguratorTest, RefreshDnsForCurrentConfigurationTriggersDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  configurator_->refreshDns(configuration_key);
}

TEST_F(ConfiguratorTest, RefreshDnsForStaleConfigurationDoesntTriggerDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts()).Times(0);
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  configurator_->refreshDns(configuration_key - 1);
}

TEST_F(ConfiguratorTest,
       ReportNetworkUsageDoesntAlterNetworkConfigurationWhenBoundInterfacesAreDisabled) {
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  configurator_->setInterfaceBindingEnabled(false);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);
  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageTriggersOverrideAfterFirstFaultAfterNetworkUpdate) {
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  configurator_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(DefaultPreferredNetworkMode, configurator_->getSocketMode());

  configurator_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, configurator_->getConfigurationKey());
  EXPECT_EQ(AlternateBoundInterfaceMode, configurator_->getSocketMode());
}

TEST_F(ConfiguratorTest, ReportNetworkUsageDisablesOverrideAfterFirstFaultAfterOverride) {
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
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
  envoy_netconf_t configuration_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
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
  envoy_netconf_t stale_key = Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
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
