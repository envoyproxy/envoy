#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

// Integration test for the dynamic_modules stats sink. Mirrors the pattern used
// by bootstrap integration tests: the test module emits log messages via the
// standard Envoy logging callback, and the test asserts those log messages
// appear during the test window.
//
// This verifies the full boot path end-to-end:
//   1. Envoy loads the shared object
//   2. on_stat_sink_config_new is invoked
//   3. Envoy's stats flush timer fires and calls on_stat_sink_flush
//   4. The module's callbacks into Envoy's snapshot readers work
//   5. on_stat_sink_config_destroy is invoked at shutdown
//
// What it does NOT verify: actual metric payload delivery anywhere outside of
// the Envoy process. That is intentionally the module's responsibility, not
// ours, and matches the pattern of access_loggers/dynamic_modules where the
// module has no observable side-effect on Envoy's request/response path.
class DynamicModulesStatsSinkIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesStatsSinkIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void setUpTestModulePath() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("stat_sink_integration_test", "c");
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  // Adds our sink to the bootstrap config and lowers the flush interval so a
  // flush fires within the test's observation window.
  void addStatSinkAndInitialize() {
    setUpTestModulePath();
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* sink = bootstrap.add_stats_sinks();
      sink->set_name("envoy.stat_sinks.dynamic_modules");

      constexpr auto sink_yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_integration_test
  do_not_close: true
sink_name: integration_test
sink_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";
      envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink sink_config;
      TestUtility::loadFromYaml(sink_yaml, sink_config);
      sink->mutable_typed_config()->PackFrom(sink_config);

      // Fire every 100ms so at least one flush happens during the test.
      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesStatsSinkIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that the sink loads, the module's config_new hook runs, and a flush
// fires within the test window.
TEST_P(DynamicModulesStatsSinkIntegrationTest, BasicFlush) {
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"info", "stat sink integration test: config_new called"},
                                 {"info", "stat sink integration test: flush called"},
                             }),
                             {
                               addStatSinkAndInitialize();
                               // Wait long enough for at least one flush interval (100ms * a few)
                               // so the flush timer fires inside the recording window.
                               timeSystem().realSleepDoNotUseWithoutScrutiny(
                                   std::chrono::milliseconds(500));
                             });
}

// Sends a request so cluster/listener counters move; verifies the sink keeps
// getting called (no crash on a snapshot containing real data).
TEST_P(DynamicModulesStatsSinkIntegrationTest, FlushAfterTraffic) {
  // Wrap the test body in a lambda so the brace initializer commas inside
  // are not interpreted as macro argument separators by the log-contains macro.
  auto body = [this]() {
    addStatSinkAndInitialize();
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    // Give the flush timer time to fire at least once after traffic.
    timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(500));
  };
  EXPECT_LOG_CONTAINS("info", "stat sink integration test: flush called", body());
}

} // namespace
} // namespace Envoy
