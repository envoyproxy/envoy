#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"

#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {
namespace {

envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig
parseUdpProxyConfigFromYaml(const std::string& yaml) {
  envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig config;
  TestUtility::loadFromYaml(yaml, config);
  TestUtility::validate(config);
  return config;
}

Network::Address::InstanceConstSharedPtr parseAddress(const std::string& address) {
  return Network::Utility::parseInternetAddressAndPort(address);
}

} // namespace

// Basic UDP proxy flow to a single cluster.
TEST(RouterImplTest, RouteToSingleCluster) {
  const std::string yaml = R"EOF(
stat_prefix: foo
cluster: udp_service
  )EOF";

  auto config = parseUdpProxyConfigFromYaml(yaml);
  auto router = std::make_shared<ConfigImpl>(config);

  EXPECT_EQ("udp_service",
            router->route(parseAddress("10.0.0.1:10000"))->routeEntry()->clusterName());
  EXPECT_EQ("udp_service",
            router->route(parseAddress("172.16.0.1:10000"))->routeEntry()->clusterName());
  EXPECT_EQ("udp_service",
            router->route(parseAddress("192.168.0.1:10000"))->routeEntry()->clusterName());
  EXPECT_EQ("udp_service",
            router->route(parseAddress("[fc00::1]:10000"))->routeEntry()->clusterName());
}

// Route UDP packets to clusters based on source_prefix_range.
TEST(RouterImplTest, RouteBySourcePrefix) {
  const std::string yaml = R"EOF(
stat_prefix: foo
route_config:
  routes:
  - match:
      source_prefix_ranges:
      - address_prefix: 10.0.0.0
        prefix_len: 8
    route:
      cluster: udp_service
  - match:
      source_prefix_ranges:
      - address_prefix: 172.16.0.0
        prefix_len: 16
    route:
      cluster: udp_service2
  )EOF";

  auto config = parseUdpProxyConfigFromYaml(yaml);
  auto router = std::make_shared<ConfigImpl>(config);

  EXPECT_EQ("udp_service",
            router->route(parseAddress("10.0.0.1:10000"))->routeEntry()->clusterName());
  EXPECT_EQ("udp_service2",
            router->route(parseAddress("172.16.0.1:10000"))->routeEntry()->clusterName());
  EXPECT_EQ(nullptr, router->route(parseAddress("192.168.0.1:10000")));
  EXPECT_EQ(nullptr, router->route(parseAddress("[fc00::1]:10000")));
}

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
