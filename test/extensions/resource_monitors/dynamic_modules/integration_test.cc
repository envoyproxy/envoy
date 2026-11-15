#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::Eq;
using testing::Ge;
using testing::Lt;

constexpr absl::string_view PressureRuntimeKey = "test.dynamic_module_monitor.pressure";

class DynamicModuleResourceMonitorIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleResourceMonitorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  // Monitors the dynamic_module_monitor resource with the given module implementation, which reads
  // its pressure from the runtime, and adds the given overload manager fields.
  void initializeOverloadManager(const std::string& resource_monitor_name,
                                 const std::string& overload_manager_fields) {
    initializeOverloadManager(resource_monitor_name, std::string(PressureRuntimeKey),
                              overload_manager_fields);
  }

  void initializeOverloadManager(const std::string& resource_monitor_name,
                                 const std::string& resource_monitor_config,
                                 const std::string& overload_manager_fields) {
    initializeOverloadManager("resource_monitor_test", resource_monitor_name,
                              resource_monitor_config, overload_manager_fields);
  }

  void initializeOverloadManager(const std::string& module_name,
                                 const std::string& resource_monitor_name,
                                 const std::string& resource_monitor_config,
                                 const std::string& overload_manager_fields) {
    const std::string yaml = absl::StrCat(R"EOF(
refresh_interval: 0.01s
resource_monitors:
- name: dynamic_module_monitor
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.resource_monitors.dynamic_modules.v3.DynamicModuleResourceMonitor
    dynamic_module_config:
      name: )EOF",
                                          module_name, R"EOF(
      do_not_close: true
    resource_monitor_name: )EOF",
                                          resource_monitor_name, R"EOF(
    resource_monitor_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: ")EOF",
                                          resource_monitor_config, "\"\n", overload_manager_fields);
    const auto overload_manager =
        TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(yaml);
    config_helper_.addConfigModifier(
        [overload_manager](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          *bootstrap.mutable_overload_manager() = overload_manager;
        });
    initialize();
  }

  void setPressure(double pressure) {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "POST",
        absl::StrCat("/runtime_modify?", PressureRuntimeKey, "=", pressure), "",
        downstreamProtocol(), version_);
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  void expectOverloadedResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    EXPECT_EQ("envoy overloaded", response->body());
    codec_client_->close();
  }

  // Monitors the memory of the given module with the given configuration of its memory monitor.
  // The HTTP filter of the same module answers every request, so that the filters and the resource
  // monitor share the module and its allocator.
  void initializeMemoryMonitor(const std::string& module_name,
                               const std::string& resource_monitor_config) {
    config_helper_.prependFilter(fmt::format(R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: {}
  filter_name: memory
)EOF",
                                             module_name));
    initializeOverloadManager(module_name, "memory", resource_monitor_config, "");
  }

  // Sends a request with the given header to the HTTP filter of the module and expects the reply to
  // have the given status.
  void expectReply(const std::string& header, const std::string& value, absl::string_view status) {
    Http::TestRequestHeaderMapImpl headers = default_request_headers_;
    headers.addCopy(header, value);
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(status, response->headers().getStatusValue());
    codec_client_->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleResourceMonitorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleResourceMonitorIntegrationTest, RecordsPressure) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeOverloadManager("runtime_pressure", "");

  setPressure(0.42);
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Eq(42));

  // A pressure above 1 is recorded as 1.
  setPressure(3.5);
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Eq(100));
}

TEST_P(DynamicModuleResourceMonitorIntegrationTest, ShedsLoadAtLoadShedPoint) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeOverloadManager("runtime_pressure", R"EOF(
loadshed_points:
- name: envoy.load_shed_points.http_connection_manager_decode_headers
  triggers:
  - name: dynamic_module_monitor
    threshold:
      value: 0.9
)EOF");

  setPressure(0.95);
  test_server_->waitForGauge(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.scale_percent",
      Eq(100));
  expectOverloadedResponse();
  test_server_->waitForCounter(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.shed_load_count",
      Eq(1));
}

TEST_P(DynamicModuleResourceMonitorIntegrationTest, ReportsPendingUpdatesFromModuleThread) {
  DISABLE_IF_ADMIN_DISABLED;
  initializeOverloadManager("pending_runtime_pressure", "");

  setPressure(0.6);
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Eq(60));
}

TEST_P(DynamicModuleResourceMonitorIntegrationTest, ShedsLoadWithSynchronousFeedback) {
  // The module accepts two requests and reports full pressure afterwards.
  initializeOverloadManager("admission", "2", R"EOF(
loadshed_points:
- name: envoy.load_shed_points.http_connection_manager_decode_headers
  triggers:
  - name: dynamic_module_monitor
    threshold:
      value: 1.0
)EOF");

  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    EXPECT_EQ("200", response->headers().getStatusValue());
    codec_client_->close();
  }
  expectOverloadedResponse();
  test_server_->waitForCounter(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.shed_load_count",
      Eq(1));
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Eq(100));
}

TEST_P(DynamicModuleResourceMonitorIntegrationTest, ReportsMemoryRetainedByFilters) {
  initializeMemoryMonitor("memory_tracking_integration_test", "1048576");

  expectReply("x-retain-bytes", "786432", "200");
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Ge(75));

  expectReply("x-release", "true", "200");
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Lt(10));
}

TEST_P(DynamicModuleResourceMonitorIntegrationTest, RefusesMemoryBeyondHardLimit) {
  initializeMemoryMonitor("memory_tracking_integration_test", "1048576 2097152");

  // The budget does not refuse allocations, so the module can retain more than it.
  expectReply("x-retain-bytes", "1572864", "200");
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Eq(100));

  expectReply("x-retain-bytes", "1048576", "507");
  expectReply("x-release", "true", "200");
  test_server_->waitForGauge("overload.dynamic_module_monitor.pressure", Lt(10));
}

} // namespace
} // namespace Envoy
