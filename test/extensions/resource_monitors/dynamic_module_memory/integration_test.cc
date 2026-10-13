#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

class DynamicModuleMemoryIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleMemoryIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithModule(const std::string& module_name) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
    config_helper_.addConfigModifier(
        [module_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          *bootstrap.mutable_overload_manager() =
              TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(
                  fmt::format(R"EOF(
refresh_interval: 0.1s
resource_monitors:
- name: envoy.resource_monitors.dynamic_module_memory
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.resource_monitors.dynamic_module_memory.v3.DynamicModuleMemoryConfig
    module_name: {}
    max_heap_size_bytes: 67108864
)EOF",
                              module_name));
        });
    config_helper_.prependFilter(fmt::format(R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: {}
  filter_name: memory
)EOF",
                                             module_name));
    HttpIntegrationTest::initialize();
  }

  void sendRequestWithHeader(const std::string& name, const std::string& value) {
    Http::TestRequestHeaderMapImpl headers = default_request_headers_;
    headers.addCopy(name, value);
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  void testPressureTracksRetainedMemory(const std::string& module_name) {
    initializeWithModule(module_name);
    codec_client_ = makeHttpConnection(lookupPort("http"));
    const std::string pressure = "overload.envoy.resource_monitors.dynamic_module_memory.pressure";

    // Retaining twice max_heap_size_bytes puts the module at a pressure of about 200 percent.
    sendRequestWithHeader("x-retain-bytes", "134217728");
    test_server_->waitForGauge(pressure, testing::Ge(150));

    sendRequestWithHeader("x-release", "true");
    test_server_->waitForGauge(pressure, testing::Le(50));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleMemoryIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

#if defined(TCMALLOC)
TEST_P(DynamicModuleMemoryIntegrationTest, PressureTracksEstimatedHeapMemoryOfModule) {
  testPressureTracksRetainedMemory("memory_integration_test");
}
#endif

TEST_P(DynamicModuleMemoryIntegrationTest, PressureTracksReportedHeapMemoryOfModule) {
  testPressureTracksRetainedMemory("memory_tracking_integration_test");
}

} // namespace
} // namespace Envoy
