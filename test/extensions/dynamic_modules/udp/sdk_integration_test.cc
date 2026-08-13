#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {
namespace {

// Exercises the UDP listener filter SDKs end to end against a real dynamic module built from the
// per-language test_data directory. The parameter selects which SDK's module is loaded.
class DynamicModulesUdpListenerSdkIntegrationTest : public testing::TestWithParam<std::string>,
                                                    public BaseIntegrationTest {
public:
  DynamicModulesUdpListenerSdkIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4,
                            ConfigHelper::baseUdpListenerConfig()) {}

protected:
  void initializeFilter(const std::string& filter_name) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam()),
        1);

    FakeUpstreamConfig::UdpConfig config;
    setUdpFakeUpstream(config);

    // ConfigHelper::addListenerFilter moves the filter it adds to the front of the chain, so these
    // are added back to front to end up with [dynamic_modules, udp_proxy]. The dynamic module
    // filter has to run first: ActiveRawUdpListener::onDataWorker stops iterating on
    // StopIteration, which is what keeps a dropped datagram away from udp_proxy.
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

    config_helper_.addListenerFilter(fmt::format(R"EOF(
name: envoy.filters.udp_listener.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter
  dynamic_module_config:
    name: "udp_listener_integration_test"
    do_not_close: true
  filter_name: "{}"
)EOF",
                                                 filter_name));

    BaseIntegrationTest::initialize();
  }

  Network::Address::InstanceConstSharedPtr listenerAddress() {
    return *Network::Utility::resolveUrl(fmt::format(
        "udp://{}:{}", Network::Test::getLoopbackAddressUrlString(Network::Address::IpVersion::v4),
        lookupPort("listener_0")));
  }
};

// Only the Go SDK ships a udp_listener_integration_test module today. Add "rust" and "cpp" here
// once the equivalent modules exist in their test_data directories.
INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModulesUdpListenerSdkIntegrationTest,
                         testing::Values("go"),
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

// The module reads the datagram, its peer address and its local address, sends the payload straight
// back to the sender, and stops iteration so udp_proxy never forwards it upstream.
TEST_P(DynamicModulesUdpListenerSdkIntegrationTest, EchoDatagram) {
  initializeFilter("echo_datagram");

  const std::string request = "hello";
  Network::Test::UdpSyncPeer client(Network::Address::IpVersion::v4);
  client.write(request, *listenerAddress());

  Network::UdpRecvData response;
  client.recv(response);
  EXPECT_EQ(request, response.buffer_->toString());

  // StopIteration kept the datagram away from udp_proxy, so the upstream saw nothing.
  Network::UdpRecvData upstream_datagram;
  EXPECT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(upstream_datagram, std::chrono::milliseconds(500)));
}

// The module replaces the datagram payload and continues iteration, so udp_proxy forwards the
// rewritten bytes upstream. It also touches every metrics callback and the worker index.
TEST_P(DynamicModulesUdpListenerSdkIntegrationTest, RewriteDatagram) {
  initializeFilter("rewrite_datagram");

  const std::string request = "hello";
  Network::Test::UdpSyncPeer client(Network::Address::IpVersion::v4);
  client.write(request, *listenerAddress());

  Network::UdpRecvData upstream_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(upstream_datagram));
  EXPECT_EQ("rewritten", upstream_datagram.buffer_->toString());

  // The UDP filter config scopes its metrics as "<namespace>.<filter_name>.", defaulting the
  // namespace to DefaultMetricsNamespace. See DynamicModuleUdpListenerFilterConfig's constructor.
  test_server_->waitForCounter("dynamicmodulescustom.rewrite_datagram.datagrams_rewritten",
                               testing::Eq(1));
  EXPECT_EQ(
      request.size(),
      test_server_->gauge("dynamicmodulescustom.rewrite_datagram.last_datagram_size")->value());
}

} // namespace
} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
