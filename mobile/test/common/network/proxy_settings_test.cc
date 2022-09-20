#include "gtest/gtest.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

class ProxySettingsTest : public testing::Test {
public:
  ProxySettingsTest() {}
};

TEST_F(ProxySettingsTest, SameIPv4AddressesAndPortsAreEqual) {
  EXPECT_EQ(ProxySettings("127.0.0.1", 2222), ProxySettings("127.0.0.1", 2222));
}

TEST_F(ProxySettingsTest, DifferentPortsAreNotEqual) {
  EXPECT_NE(ProxySettings("127.0.0.1", 1111), ProxySettings("127.0.0.1", 2222));
}

TEST_F(ProxySettingsTest, DifferentAddressesAreNotEqual) {
  EXPECT_NE(ProxySettings("127.0.0.2", 1111), ProxySettings("127.0.0.1", 1111));
}

TEST_F(ProxySettingsTest, EmptyAddressStringResultsInNullAddress) {
  EXPECT_EQ(ProxySettings("", 0).address(), nullptr);
  EXPECT_EQ(ProxySettings("", 0).asString(), "no_proxy_configured");
}

} // namespace Network
} // namespace Envoy
