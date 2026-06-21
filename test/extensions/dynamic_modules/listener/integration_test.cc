#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/network/echo/v3/echo.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {

class DynamicModulesListenerSdkIntegrationTest : public testing::TestWithParam<std::string>,
                                                 public BaseIntegrationTest {
public:
  DynamicModulesListenerSdkIntegrationTest()
      : BaseIntegrationTest(GetParam() == "cpp" ? Network::Address::IpVersion::v4
                                                : Network::Address::IpVersion::v6,
                            ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      - filters:
        - name: envoy.filters.network.echo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF") {}

protected:
  void initializeFilter(const std::string& filter_name) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam()),
        1);

    config_helper_.addConfigModifier([filter_name](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      filter->set_name("envoy.filters.listener.dynamic_modules");

      envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter
          dynamic_module_config;
      dynamic_module_config.mutable_dynamic_module_config()->set_name("listener_integration_test");
      dynamic_module_config.set_filter_name(filter_name);
      filter->mutable_typed_config()->PackFrom(dynamic_module_config);
    });

    BaseIntegrationTest::initialize();
  }
};

#ifndef __SANITIZE_ADDRESS__
auto DynamicModulesListenerSdkIntegrationTestValues = testing::Values("rust", "go", "cpp");
#else
auto DynamicModulesListenerSdkIntegrationTestValues = testing::Values("rust", "go");
#endif

INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModulesListenerSdkIntegrationTest,
                         DynamicModulesListenerSdkIntegrationTestValues,
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(DynamicModulesListenerSdkIntegrationTest, WriteToSocket) {
  initializeFilter("write_to_socket");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

TEST_P(DynamicModulesListenerSdkIntegrationTest, BufferRead) {
  initializeFilter("buffer_read");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

// Regression test for the listener filter shared-ownership fix. A make_unique-owned listener filter
// aborted the worker when a module issued an HTTP callout, because sendHttpCallout calls
// shared_from_this which throws std::bad_weak_ptr on a non-shared-owned object. The production
// factory now shares ownership via an adapter. The autonomous upstream services the callout so the
// filter resumes the accept. Rust-only because the bug is in the language-agnostic C++ factory and
// one module exercises it.
TEST_P(DynamicModulesListenerSdkIntegrationTest, HttpCalloutOnAcceptDoesNotCrash) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the http_callout_on_accept filter is only in the rust test module";
  }
  autonomous_upstream_ = true;
  initializeFilter("http_callout_on_accept");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

} // namespace Envoy
