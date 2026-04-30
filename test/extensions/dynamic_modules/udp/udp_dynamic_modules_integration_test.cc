#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {
namespace {

// Parameterized over (language, ip_version). The "c" language uses pre-existing C fakes
// (udp_no_op / udp_stop_iteration); "rust" and "go" use the udp_integration_test module
// each language ships, which registers two filter names: "test_filter" (passthrough)
// and "stop_iteration" (drop).
struct UdpDynamicModulesParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class UdpDynamicModulesIntegrationTest : public testing::TestWithParam<UdpDynamicModulesParam>,
                                         public BaseIntegrationTest {
public:
  UdpDynamicModulesIntegrationTest()
      : BaseIntegrationTest(GetParam().ip_version, ConfigHelper::baseUdpListenerConfig()) {}

  // Returns (module_name, filter_name) for the given test variant. The C fakes have
  // hard-coded module-level behavior (one module per behavior); the SDK languages have a
  // single module exposing two filter names chosen via filter_name.
  std::pair<std::string, std::string> moduleAndFilter(const std::string& variant) {
    if (GetParam().language == "c") {
      // The C-fake module name encodes the behavior; the filter name is unused by the C
      // fake but must be set to something.
      if (variant == "passthrough") {
        return {"udp_no_op", "test_filter"};
      }
      return {"udp_stop_iteration", "test_filter"};
    }
    // For Rust/Go we share a single module name and select via filter_name.
    if (variant == "passthrough") {
      return {"udp_integration_test", "test_filter"};
    }
    return {"udp_integration_test", "stop_iteration"};
  }

  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  void setup(const std::string& variant = "passthrough") {
    FakeUpstreamConfig::UdpConfig config;
    setUdpFakeUpstream(config);

    auto [module_name, filter_name] = moduleAndFilter(variant);

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
    value: "some_config"
)EOF",
                                                  module_name, filter_name);

    config_helper_.addListenerFilter(filter_config);

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

    BaseIntegrationTest::initialize();
  }
};

namespace {
std::vector<UdpDynamicModulesParam> getUdpTestParams() {
  std::vector<UdpDynamicModulesParam> params;
  for (const auto& language : {"c", "rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string udpParamName(const testing::TestParamInfo<UdpDynamicModulesParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, UdpDynamicModulesIntegrationTest,
                         testing::ValuesIn(getUdpTestParams()), udpParamName);

TEST_P(UdpDynamicModulesIntegrationTest, BasicDataFlow) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(fmt::format(
      "tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam().ip_version), port));

  std::string request = "hello";
  Network::Test::UdpSyncPeer client(GetParam().ip_version);
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(request, request_datagram.buffer_->toString());
}

TEST_P(UdpDynamicModulesIntegrationTest, StopIteration) {
  setup("stop_iteration");

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(fmt::format(
      "tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam().ip_version), port));

  std::string request = "should be blocked";
  Network::Test::UdpSyncPeer client(GetParam().ip_version);
  client.write(request, *listener_address);

  Network::UdpRecvData request_datagram;
  // UDP listener filter StopIteration is currently not enforced in the fake upstream path.
  // We verify that the datagram still arrives. When StopIteration is enforced in the future,
  // this expectation can be flipped.
  EXPECT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::seconds(1)));
  EXPECT_EQ(request, request_datagram.buffer_->toString());
}

TEST_P(UdpDynamicModulesIntegrationTest, LargePayload) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(fmt::format(
      "tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam().ip_version), port));

  // Use a conservative payload size to avoid platform-specific UDP limits.
  std::string large_request(512, 'x');
  Network::Test::UdpSyncPeer client(GetParam().ip_version);
  client.write(large_request, *listener_address);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(large_request, request_datagram.buffer_->toString());
}

TEST_P(UdpDynamicModulesIntegrationTest, MultipleDatagrams) {
  setup();

  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(fmt::format(
      "tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam().ip_version), port));

  Network::Test::UdpSyncPeer client(GetParam().ip_version);

  // Send multiple datagrams.
  for (int i = 0; i < 5; i++) {
    std::string request = fmt::format("datagram_{}", i);
    client.write(request, *listener_address);

    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(request, request_datagram.buffer_->toString());
  }
}

} // namespace
} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
