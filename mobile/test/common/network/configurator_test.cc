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

TEST_F(ConfiguratorTest, RefreshDnsForCurrentNetworkTriggersDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts());
  Configurator::setPreferredNetwork(ENVOY_NET_WWAN);
  configurator_->refreshDns(ENVOY_NET_WWAN);
}

TEST_F(ConfiguratorTest, RefreshDnsForOtherNetworkDoesntTriggerDnsRefresh) {
  EXPECT_CALL(*dns_cache_, forceRefreshHosts()).Times(0);
  Configurator::setPreferredNetwork(ENVOY_NET_WLAN);
  configurator_->refreshDns(ENVOY_NET_WWAN);
}

TEST_F(ConfiguratorTest, EnumerateInterfacesFiltersByFlags) {
  // Select loopback.
  auto loopbacks = configurator_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, 0);
  EXPECT_EQ(loopbacks[0].rfind("lo", 0), 0);

  // Reject loopback.
  auto nonloopbacks = configurator_->enumerateInterfaces(AF_INET, 0, IFF_LOOPBACK);
  for (const auto& interface : nonloopbacks) {
    EXPECT_NE(interface.rfind("lo", 0), 0);
  }

  // Select AND reject loopback.
  auto empty = configurator_->enumerateInterfaces(AF_INET, IFF_LOOPBACK, IFF_LOOPBACK);
  EXPECT_EQ(empty.size(), 0);
}

} // namespace Network
} // namespace Envoy
