#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"

#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {
namespace {

class RouterImplTest : public ::testing::Test {
public:
  void setup(const std::string& yaml) {
    auto config = parseUdpProxyConfigFromYaml(yaml);
    router_ = std::make_shared<RouterImpl>(config, factory_context_);
  }

  std::shared_ptr<RouterImpl> router_;

protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

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
};

// Basic UDP proxy flow to a single cluster.
TEST_F(RouterImplTest, DEPRECATED_FEATURE_TEST(RouteToSingleCluster)) {
  const std::string yaml = R"EOF(
stat_prefix: foo
cluster: udp_service
  )EOF";

  setup(yaml);

  EXPECT_EQ("udp_service",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("10.0.0.1:10000")));
  EXPECT_EQ("udp_service",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("172.16.0.1:10000")));
  EXPECT_EQ("udp_service",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("192.168.0.1:10000")));
  EXPECT_EQ("udp_service",
            router_->route(parseAddress("[::]:80"), parseAddress("[fc00::1]:10000")));
}

// Route UDP packets to multiple clusters.
TEST_F(RouterImplTest, RouteToMultipleClusters) {
  const std::string yaml = R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.source_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service
        "172.16.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service2
  )EOF";

  setup(yaml);

  EXPECT_EQ("udp_service",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("10.0.0.1:10000")));
  EXPECT_EQ("udp_service2",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("172.16.0.1:10000")));
  EXPECT_EQ("", router_->route(parseAddress("0.0.0.0:80"), parseAddress("192.168.0.1:10000")));
  EXPECT_EQ("", router_->route(parseAddress("[::]:80"), parseAddress("[fc00::1]:10000")));
}

// Route UDP packets to multiple clusters with on_no_match set.
TEST_F(RouterImplTest, RouteOnNoMatch) {
  const std::string yaml = R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.source_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service
        "172.16.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service2
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: udp_service3
  )EOF";

  setup(yaml);

  EXPECT_EQ("udp_service",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("10.0.0.1:10000")));
  EXPECT_EQ("udp_service2",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("172.16.0.1:10000")));
  EXPECT_EQ("udp_service3",
            router_->route(parseAddress("0.0.0.0:80"), parseAddress("192.168.0.1:10000")));
  EXPECT_EQ("udp_service3",
            router_->route(parseAddress("[::]:80"), parseAddress("[fc00::1]:10000")));
}

// All cluster names in the router with a single cluster.
TEST_F(RouterImplTest, DEPRECATED_FEATURE_TEST(SingleClusterEntry)) {
  const std::string yaml = R"EOF(
stat_prefix: foo
cluster: udp_service
  )EOF";

  setup(yaml);

  ASSERT_THAT(router_->allClusterNames(), testing::UnorderedElementsAre("udp_service"));
}

// All cluster names in the router with multiple cluster.
TEST_F(RouterImplTest, MultipleClusterEntry) {
  const std::string yaml = R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.source_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service
        "172.16.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service2
  )EOF";

  setup(yaml);

  ASSERT_THAT(router_->allClusterNames(),
              testing::UnorderedElementsAre("udp_service", "udp_service2"));
}

// All cluster names in the router with multiple cluster and on_no_match set.
TEST_F(RouterImplTest, OnNoMatchEntry) {
  const std::string yaml = R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.source_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service
        "172.16.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service2
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: udp_service3
  )EOF";

  setup(yaml);

  ASSERT_THAT(router_->allClusterNames(),
              testing::UnorderedElementsAre("udp_service", "udp_service2", "udp_service3"));
}

// Error on invalid data input.
TEST_F(RouterImplTest, InvalidDataInput) {
  const std::string yaml = R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.destination_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: udp_service
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setup(yaml), EnvoyException,
      "requirement violation while creating route match tree: INVALID_ARGUMENT: Route table can "
      "only match on source IP, saw "
      "type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput");
}

} // namespace
} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
