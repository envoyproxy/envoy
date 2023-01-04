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

TEST_F(ProxySettingsTest, HostnamesAreEqual) {
  EXPECT_EQ(ProxySettings("foo.com", 2222), ProxySettings("foo.com", 2222));
}

TEST_F(ProxySettingsTest, MixUpAddressesAndHostnames) {
  EXPECT_NE(ProxySettings("127.0.0.1", 2222), ProxySettings("", 0));
  EXPECT_NE(ProxySettings("127.0.0.1", 2222), ProxySettings("", 2222));
  EXPECT_NE(ProxySettings("127.0.0.1", 2222), ProxySettings("foo.com", 2222));
}

TEST_F(ProxySettingsTest, HostnamesWithDifferentPortsAreNotEqual) {
  EXPECT_NE(ProxySettings("foo.com", 2), ProxySettings("foo.com", 2222));
}

TEST_F(ProxySettingsTest, DifferentHostnamesAreNotEqual) {
  EXPECT_NE(ProxySettings("bar.com", 2222), ProxySettings("foo.com", 2222));
}

TEST_F(ProxySettingsTest, DifferentAddressesAreNotEqual) {
  EXPECT_NE(ProxySettings("127.0.0.2", 1111), ProxySettings("127.0.0.1", 1111));
}

TEST_F(ProxySettingsTest, EmptyAddressStringResultsInNullAddress) {
  EXPECT_EQ(ProxySettings("", 0).address(), nullptr);
  EXPECT_EQ(ProxySettings("", 0).asString(), "no_proxy_configured");
}

TEST_F(ProxySettingsTest, Hostname) {
  EXPECT_EQ(ProxySettings("foo.com", 0).address(), nullptr);
  EXPECT_EQ(ProxySettings("foo.com", 80).asString(), "foo.com:80");
}

} // namespace Network
} // namespace Envoy
