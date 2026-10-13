#include <vector>

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

TEST_F(ProxySettingsTest, Direct) {
  const ProxySettings direct = ProxySettings::direct();
  EXPECT_TRUE(direct.isDirect());
  EXPECT_EQ(direct.hostname(), "");
  EXPECT_EQ(direct.port(), 0);
  EXPECT_EQ(direct.address(), nullptr);
  EXPECT_EQ(direct.asString(), "no_proxy_configured");

  const ProxySettings non_direct = ProxySettings("foo.com", 80);
  EXPECT_FALSE(non_direct.isDirect());
}

TEST_F(ProxySettingsTest, Create) {
  std::vector<ProxySettings> settings_list;

  // Empty list, so nullptr is returned.
  EXPECT_EQ(ProxySettings::create(settings_list), nullptr);

  // 2nd setting in the list is non-direct, so it should be chosen to create the shared_ptr from.
  settings_list.emplace_back(ProxySettings::direct());
  settings_list.emplace_back(ProxySettings("foo.com", 80));
  settings_list.emplace_back(ProxySettings("bar.com", 8080));
  ProxySettingsConstSharedPtr settings = ProxySettings::create(settings_list);
  EXPECT_EQ(settings->hostname(), "foo.com");
  EXPECT_EQ(settings->port(), 80);
  EXPECT_FALSE(settings->isDirect());

  // All ProxySettings in the list are direct, so nullptr is returned.
  settings_list.clear();
  settings_list.emplace_back(ProxySettings::direct());
  settings_list.emplace_back(ProxySettings::direct());
  EXPECT_EQ(ProxySettings::create(settings_list), nullptr);
}

} // namespace Network
} // namespace Envoy
