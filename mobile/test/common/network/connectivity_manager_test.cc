#include <net/if.h>
#include <sys/types.h>

#include "test/common/mocks/common/mocks.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gtest/gtest.h"
#include "library/common/network/connectivity_manager.h"

using testing::_;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Network {

class MockNetworkConnectivityObserver : public Quic::QuicNetworkConnectivityObserver {
public:
  MOCK_METHOD(void, onNetworkMadeDefault, (NetworkHandle network), (override));
  MOCK_METHOD(void, onNetworkDisconnected, (NetworkHandle network), (override));
  MOCK_METHOD(void, onNetworkConnected, (NetworkHandle network), (override));
};

class ConnectivityManagerTest : public testing::Test {
public:
  ConnectivityManagerTest()
      : dns_cache_manager_(
            new NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>()),
        dns_cache_(dns_cache_manager_->dns_cache_),
        helper_handle_(test::SystemHelperPeer::replaceSystemHelper()) {

    EXPECT_CALL(helper_handle_->mock_helper(), getDefaultNetworkHandle()).WillOnce(Return(1));
    std::vector<std::pair<int64_t, ConnectionType>> connected_networks{
        {1, ConnectionType::CONNECTION_WIFI}, {2, ConnectionType::CONNECTION_UNKNOWN}};
    EXPECT_CALL(helper_handle_->mock_helper(), getAllConnectedNetworks())
        .WillOnce(Return(connected_networks));
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.mobile_use_network_observer_registry")) {
      EXPECT_CALL(cm_, createNetworkObserverRegistries(_))
          .WillOnce(Invoke([this](Quic::EnvoyQuicNetworkObserverRegistryFactory& registry_factory) {
            registry_.reset(static_cast<Quic::EnvoyMobileQuicNetworkObserverRegistry*>(
                registry_factory.createQuicNetworkObserverRegistry(dispatcher_).release()));
            registry_->registerObserver(observer_);
          }));
    }
    connectivity_manager_ = std::make_shared<ConnectivityManagerImpl>(cm_, dns_cache_manager_);
    ON_CALL(*dns_cache_manager_, lookUpCacheByName(_)).WillByDefault(Return(dns_cache_));
    // Toggle network to reset network state.
    connectivity_manager_->setPreferredNetwork(1);
    connectivity_manager_->setPreferredNetwork(2);

    // Set up the default network change callback.
    auto callback = [&](envoy_netconf_t key) {
      EXPECT_EQ(key, connectivity_manager_->getConfigurationKey());
      num_default_network_change_++;
    };
    connectivity_manager_->setDefaultNetworkChangeCallback(std::move(callback));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<NiceMock<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager>>
      dns_cache_manager_;
  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCache> dns_cache_;
  NiceMock<Upstream::MockClusterManager> cm_{};
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
  ConnectivityManagerImplSharedPtr connectivity_manager_;
  testing::StrictMock<MockNetworkConnectivityObserver> observer_;
  // Track callback invocation count.
  int num_default_network_change_{0};
  Quic::EnvoyMobileQuicNetworkObserverRegistryPtr registry_;
};

TEST_F(ConnectivityManagerTest, SetPreferredNetworkWithNewNetworkChangesConfigurationKey) {
  envoy_netconf_t original_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t new_key = connectivity_manager_->setPreferredNetwork(4);
  EXPECT_NE(original_key, new_key);
  EXPECT_EQ(new_key, connectivity_manager_->getConfigurationKey());
}

TEST_F(ConnectivityManagerTest,
       DISABLED_SetPreferredNetworkWithUnchangedNetworkReturnsStaleConfigurationKey) {
  envoy_netconf_t original_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t stale_key = connectivity_manager_->setPreferredNetwork(2);
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
  Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks* dns_completion_callback{
      nullptr};
  EXPECT_CALL(*dns_cache_, addUpdateCallbacks_(_))
      .WillOnce(Invoke([&dns_completion_callback](
                           Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks& cb) {
        dns_completion_callback = &cb;
        return nullptr;
      }));
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

  EXPECT_CALL(cm_, drainConnections(_, ConnectionPool::DrainBehavior::DrainExistingConnections));
  dns_completion_callback->onDnsResolutionComplete(
      "cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Completed);
  dns_completion_callback->onDnsResolutionComplete(
      "not-cached.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Completed);
  dns_completion_callback->onDnsResolutionComplete(
      "not-cached2.example.com",
      std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>(),
      Network::DnsResolver::ResolutionStatus::Completed);
}

TEST_F(ConnectivityManagerTest, WhenDrainPostDnsNotEnabledDoesntDrainPostDnsRefresh) {
  EXPECT_CALL(*dns_cache_, addUpdateCallbacks_(_)).Times(0);
  connectivity_manager_->setDrainPostDnsRefreshEnabled(false);

  EXPECT_CALL(*dns_cache_, iterateHostMap(_)).Times(0);
  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->refreshDns(configuration_key, true);
}

TEST_F(ConnectivityManagerTest,
       ReportNetworkUsageDoesntAlterNetworkConfigurationWhenBoundInterfacesAreDisabled) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(false);
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest,
       ReportNetworkUsageTriggersOverrideAfterFirstFaultAfterNetworkUpdate) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisablesOverrideAfterFirstFaultAfterOverride) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  configuration_key = connectivity_manager_->getConfigurationKey();
  EXPECT_EQ(SocketMode::AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisablesOverrideAfterThirdFaultAfterSuccess) {
  envoy_netconf_t configuration_key = connectivity_manager_->getConfigurationKey();
  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, false /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_EQ(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(configuration_key, true /* network_fault */);

  EXPECT_NE(configuration_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
}

TEST_F(ConnectivityManagerTest, ReportNetworkUsageDisregardsCallsWithStaleConfigurationKey) {
  envoy_netconf_t stale_key = connectivity_manager_->getConfigurationKey();
  envoy_netconf_t current_key = connectivity_manager_->setPreferredNetwork(4);
  EXPECT_NE(stale_key, current_key);

  connectivity_manager_->setInterfaceBindingEnabled(true);
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);
  connectivity_manager_->reportNetworkUsage(stale_key, true /* network_fault */);

  EXPECT_EQ(current_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::DefaultPreferredNetworkMode, connectivity_manager_->getSocketMode());

  connectivity_manager_->reportNetworkUsage(stale_key, false /* network_fault */);
  connectivity_manager_->reportNetworkUsage(current_key, true /* network_fault */);

  EXPECT_NE(current_key, connectivity_manager_->getConfigurationKey());
  EXPECT_EQ(SocketMode::AlternateBoundInterfaceMode, connectivity_manager_->getSocketMode());
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

TEST_F(ConnectivityManagerTest, NetworkChangeResultsInDifferentSocketOptionsHash) {
  auto options1 = std::make_shared<Socket::Options>();
  connectivity_manager_->addUpstreamSocketOptions(options1);
  std::vector<uint8_t> hash1;
  for (const auto& option : *options1) {
    option->hashKey(hash1);
  }
  connectivity_manager_->setPreferredNetwork(64);
  auto options2 = std::make_shared<Socket::Options>();
  connectivity_manager_->addUpstreamSocketOptions(options2);
  std::vector<uint8_t> hash2;
  for (const auto& option : *options2) {
    option->hashKey(hash2);
  }
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.decouple_explicit_drain_pools_and_dns_refresh") ||
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.drain_pools_on_network_change")) {
    EXPECT_NE(hash1, hash2);
  } else {
    EXPECT_EQ(hash1, hash2);
  }
}

// Verifies that when the platform notifies about the same default network
// again, the signal will be ignored.
TEST_F(ConnectivityManagerTest, DuplicatedSignalOfAndroidNetworkBecomesDefault) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    return;
  }
  EXPECT_CALL(observer_, onNetworkMadeDefault(_)).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  connectivity_manager_->onDefaultNetworkChangedAndroid(ConnectionType::CONNECTION_WIFI, 1);
  // The callback should not have been called.
  EXPECT_EQ(num_default_network_change_, 0);
}

// Verifies that when a network is connected and then becomes the default
// default_network_change_callback_ called at the end rather than in the middle.
TEST_F(ConnectivityManagerTest, AndroidNetworkConnectedAndThenBecomesDefault) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    return;
  }

  const NetworkHandle net_id = 123;
  const auto connection_type = ConnectionType::CONNECTION_WIFI;
  EXPECT_CALL(dispatcher_, post(_)).Times(2u);

  // Simulate a network is connected.
  EXPECT_CALL(observer_, onNetworkConnected(net_id));
  connectivity_manager_->onNetworkConnectAndroid(connection_type, net_id);
  // The callback should not have been called yet.
  EXPECT_EQ(num_default_network_change_, 0);

  // Simulate the connected network now becomes the default.
  EXPECT_CALL(observer_, onNetworkMadeDefault(net_id));
  connectivity_manager_->onDefaultNetworkChangedAndroid(connection_type, net_id);

  // Verify the callback was invoked exactly once.
  EXPECT_EQ(num_default_network_change_, 1);
}

// Verifies that when a network becomes the default without becoming connected,
// default_network_change_callback_ is not called. And it should be called once the network is
// connected.
TEST_F(ConnectivityManagerTest, AndroidNetworkBecomesDefaultAndThenConnected) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    return;
  }

  const NetworkHandle net_id = 123;
  const auto connection_type = ConnectionType::CONNECTION_4G;
  const envoy_netconf_t initial_config_key = connectivity_manager_->getConfigurationKey();
  EXPECT_CALL(dispatcher_, post(_)).Times(2u);

  // Simulate that the network becomes the default. At this point, it is not yet "connected".
  connectivity_manager_->onDefaultNetworkChangedAndroid(connection_type, net_id);

  // The callback should not have been called, and the preferred network should not have changed
  // yet.
  EXPECT_EQ(num_default_network_change_, 0);
  EXPECT_EQ(initial_config_key, connectivity_manager_->getConfigurationKey());

  // Now, simulate the network becoming connected.
  // This should trigger the deferred default network callback and update the internal state.
  EXPECT_CALL(observer_, onNetworkConnected(net_id));
  EXPECT_CALL(observer_, onNetworkMadeDefault(net_id));
  connectivity_manager_->onNetworkConnectAndroid(connection_type, net_id);

  // Verify the callback was invoked.
  EXPECT_EQ(num_default_network_change_, 1);
  EXPECT_NE(initial_config_key, connectivity_manager_->getConfigurationKey());
}

// Verifies that the observer is notified about a network becoming connected and
// disconnected.
TEST_F(ConnectivityManagerTest, AndroidNetworkConnectedAndThenDisconnected) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    return;
  }

  const NetworkHandle net_id = 123;
  const auto connection_type = ConnectionType::CONNECTION_WIFI;
  EXPECT_CALL(dispatcher_, post(_)).Times(2u);

  EXPECT_CALL(observer_, onNetworkConnected(net_id));
  // Simulate a network is connected.
  connectivity_manager_->onNetworkConnectAndroid(connection_type, net_id);
  EXPECT_EQ(num_default_network_change_, 0);

  EXPECT_CALL(observer_, onNetworkDisconnected(net_id));
  connectivity_manager_->onNetworkDisconnectAndroid(net_id);

  // Disconnected network should not be used as the default.
  connectivity_manager_->onDefaultNetworkChangedAndroid(connection_type, net_id);
  EXPECT_EQ(num_default_network_change_, 0);
}

// Verifies that the observer is notified about networks becoming disconnected when they are purged.
// But if the network is exempted from purging, observer shouldn't be notified about it being
// disconnected.
TEST_F(ConnectivityManagerTest, AndroidPurgeNetworks) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    return;
  }

  EXPECT_CALL(dispatcher_, post(_)).Times(7u);

  EXPECT_CALL(observer_, onNetworkConnected(_)).Times(3);
  connectivity_manager_->onNetworkConnectAndroid(ConnectionType::CONNECTION_WIFI, 123);
  connectivity_manager_->onNetworkConnectAndroid(ConnectionType::CONNECTION_4G, 456);
  connectivity_manager_->onNetworkConnectAndroid(ConnectionType::CONNECTION_5G, 789);

  // Purge all networks other than the 5G network.
  EXPECT_CALL(observer_, onNetworkDisconnected(1));
  EXPECT_CALL(observer_, onNetworkDisconnected(2));
  EXPECT_CALL(observer_, onNetworkDisconnected(123));
  EXPECT_CALL(observer_, onNetworkDisconnected(456));
  connectivity_manager_->purgeActiveNetworkListAndroid({789});
}

} // namespace Network
} // namespace Envoy
