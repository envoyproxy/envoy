#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/base_overload_integration_test.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {
namespace {

class DynamicModulesUdpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  DynamicModulesUdpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {}

  void setDynamicModulesSearchPath(const std::string& module_name, const std::string& language) {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath(module_name, language);
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void SetUp() override { setDynamicModulesSearchPath("udp_no_op", "c"); }

  void setup(const std::string& module_name = "udp_no_op",
             const std::string& filter_name = "test_filter",
             const std::string& module_filter_config = "some_config") {
    FakeUpstreamConfig::UdpConfig config;
    setUdpFakeUpstream(config);

    const std::string filter_config = fmt::format(R"EOF(
name: envoy.filters.udp_listener.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter
  dynamic_module_config:
    name: "{}"
    do_not_close: true
  filter_name: "{}"
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "{}"
)EOF",
                                                  module_name, filter_name, module_filter_config);

    // addListenerFilter() prepends filters, so add udp_proxy first to produce
    // [dynamic_modules, udp_proxy].
    config_helper_.addListenerFilter(R"EOF(
name: envoy.filters.udp_listener.udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: service
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
)EOF");

    config_helper_.addListenerFilter(filter_config);

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesUdpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesUdpIntegrationTest, BasicDataFlow) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  std::string request = "hello";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(request, request_datagram.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, StopIteration) {
  setup("udp_stop_iteration");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  std::string request = "should be blocked";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;

  EXPECT_FALSE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::seconds(1)));
}

TEST_P(DynamicModulesUdpIntegrationTest, LargePayload) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  // Use a conservative payload size to avoid platform-specific UDP limits.
  std::string large_request(512, 'x');
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(large_request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(large_request, request_datagram.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, MultipleDatagrams) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  Network::Test::UdpSyncPeer client(GetParam());

  // Send multiple datagrams.
  for (int i = 0; i < 5; i++) {
    std::string request = fmt::format("datagram_{}", i);
    client.write(request, *listener_address);

    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(request, request_datagram.buffer_->toString());
  }
}

TEST_P(DynamicModulesUdpIntegrationTest, GoSdkEchoDatagram) {
  setDynamicModulesSearchPath("udp_listener_integration_test", "go");
  setup("udp_listener_integration_test", "echo_datagram");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  const std::string request = "hello";
  Network::Test::UdpSyncPeer client(GetParam());
  client.write(request, *listener_address);

  Network::UdpRecvData response;
  client.recv(response);
  EXPECT_EQ(request, response.buffer_->toString());
}

TEST_P(DynamicModulesUdpIntegrationTest, GoSdkRewriteDatagram) {
  setDynamicModulesSearchPath("udp_listener_integration_test", "go");
  setup("udp_listener_integration_test", "rewrite_datagram");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()), port));

  Network::Test::UdpSyncPeer client(GetParam());
  client.write("hello", *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("rewritten", request_datagram.buffer_->toString());
}

class DynamicModulesUdpLoadShedPointIntegrationTest : public BaseOverloadIntegrationTest,
                                                      public DynamicModulesUdpIntegrationTest {
public:
  // Saturates the trigger of the load shed point that the module checks.
  void initializeOverloadedLoadShedPoint(bool bypass_overload_manager) {
    setupOverloadManagerConfig(
        TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "test.dynamic_module_load_shed_point"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));
    config_helper_.addConfigModifier(
        [this, bypass_overload_manager](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          *bootstrap.mutable_overload_manager() = overload_manager_config_;
          bootstrap.mutable_static_resources()->mutable_listeners(0)->set_bypass_overload_manager(
              bypass_overload_manager);
        });
    setDynamicModulesSearchPath("udp_listener_integration_test", "go");
    setup("udp_listener_integration_test", "load_shed_point",
          "test.dynamic_module_load_shed_point");
    updateResource(0.95);
    test_server_->waitForGauge("overload.test.dynamic_module_load_shed_point.scale_percent",
                               testing::Eq(100));
  }

  Network::Address::InstanceConstSharedPtr listenerAddress() {
    return *Network::Utility::resolveUrl(
        fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()),
                    lookupPort("listener_0")));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesUdpLoadShedPointIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesUdpLoadShedPointIntegrationTest, ShedsLoad) {
  initializeOverloadedLoadShedPoint(/*bypass_overload_manager=*/false);

  Network::Test::UdpSyncPeer client(GetParam());
  client.write("shed", *listenerAddress());
  test_server_->waitForCounter("overload.test.dynamic_module_load_shed_point.shed_load_count",
                               testing::Eq(1));

  updateResource(0.8);
  test_server_->waitForGauge("overload.test.dynamic_module_load_shed_point.scale_percent",
                             testing::Eq(0));
  client.write("accepted", *listenerAddress());
  // Had the first datagram not been shed, it would arrive first, since udp_proxy forwards the
  // datagrams of a peer in order.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("accepted", request_datagram.buffer_->toString());
}

TEST_P(DynamicModulesUdpLoadShedPointIntegrationTest,
       ListenerBypassingOverloadManagerDoesNotShedLoad) {
  initializeOverloadedLoadShedPoint(/*bypass_overload_manager=*/true);

  Network::Test::UdpSyncPeer client(GetParam());
  client.write("accepted", *listenerAddress());
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("accepted", request_datagram.buffer_->toString());
  EXPECT_EQ(0, test_server_->counter("overload.test.dynamic_module_load_shed_point.shed_load_count")
                   ->value());
}

} // namespace
} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
