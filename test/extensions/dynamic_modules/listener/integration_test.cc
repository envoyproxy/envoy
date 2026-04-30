#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace DynamicModules {
namespace {

// Parameterized over (language, ip_version). Each language ships a
// listener_integration_test module exposing a "test_filter" listener filter that returns
// Continue on accept, allowing the connection to flow through to a TCP proxy upstream.
// This validates the filter is invoked end-to-end without breaking connection setup.
struct ListenerIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModulesListenerIntegrationTest
    : public testing::TestWithParam<ListenerIntegrationParam>,
      public BaseIntegrationTest {
public:
  DynamicModulesListenerIntegrationTest()
      : BaseIntegrationTest(GetParam().ip_version, ConfigHelper::tcpProxyConfig()) {}

  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  void initializeWithListenerFilter() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Insert the dynamic-module listener filter at the front of the listener filter chain.
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* lf = listener->add_listener_filters();
      lf->set_name("envoy.filters.listener.dynamic_modules");
      envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter
          filter_proto;
      filter_proto.mutable_dynamic_module_config()->set_name("listener_integration_test");
      filter_proto.set_filter_name("test_filter");
      Protobuf::StringValue value;
      value.set_value("test_config");
      filter_proto.mutable_filter_config()->PackFrom(value);
      lf->mutable_typed_config()->PackFrom(filter_proto);
    });
    BaseIntegrationTest::initialize();
  }
};

namespace {
std::vector<ListenerIntegrationParam> getListenerTestParams() {
  std::vector<ListenerIntegrationParam> params;
  // The Rust SDK currently does not export every listener-filter ABI symbol that
  // Envoy expects (e.g. envoy_dynamic_module_on_listener_filter_get_max_read_bytes
  // is missing from sdk/rust/src/listener.rs). Once the Rust SDK is brought up to
  // parity, "rust" can be added back to this list.
  for (const auto& language : {"go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string listenerParamName(const testing::TestParamInfo<ListenerIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModulesListenerIntegrationTest,
                         testing::ValuesIn(getListenerTestParams()), listenerParamName);

TEST_P(DynamicModulesListenerIntegrationTest, PassthroughTcpProxy) {
  initializeWithListenerFilter();

  IntegrationTcpClientPtr client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Round-trip a payload to confirm the listener filter passed the connection through.
  ASSERT_TRUE(client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  client->waitForData("world");

  client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

} // namespace
} // namespace DynamicModules
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
