#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

using TestParams = std::tuple<Network::Address::IpVersion, std::string>;

class DynamicModulesStatsSinkIntegrationTest : public testing::TestWithParam<TestParams>,
                                               public HttpIntegrationTest {
public:
  DynamicModulesStatsSinkIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  static std::string testParamsToString(const testing::TestParamInfo<TestParams>& info) {
    return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(info.param)),
                       std::get<1>(info.param));
  }

  std::string language() const { return std::get<1>(GetParam()); }

  std::string moduleName() const { return "stat_sink_integration_test"; }

  void setUpTestModulePath() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath(moduleName(), language());
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void addStatSinkAndInitialize() {
    setUpTestModulePath();
    // The Go SDK returns a Go pointer as the config handle, so the `cgo` pointer
    // check must be disabled, matching the other dynamic module Go tests.
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* sink = bootstrap.add_stats_sinks();
      sink->set_name("envoy.stat_sinks.dynamic_modules");

      const std::string sink_yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
sink_name: integration_test
sink_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF",
                                                moduleName());
      envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink sink_config;
      TestUtility::loadFromYaml(sink_yaml, sink_config);
      sink->mutable_typed_config()->PackFrom(sink_config);

      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    });

    HttpIntegrationTest::initialize();
  }
};

// The Rust test module aggregates flush snapshots on a worker thread and hands them back over an
// `mpsc` channel. Its shared library is not ThreadSanitizer instrumented, the same Rust toolchain
// limitation that keeps the Rust SDK unit test off TSAN, so the sanitizer cannot see the worker
// synchronization and reports false data races. The Go module hands data back through the Go heap,
// which ThreadSanitizer does not track, so it stays enabled.
std::vector<std::string> testModuleLanguages() {
#if defined(__has_feature) && __has_feature(thread_sanitizer)
  return {"c", "go"};
#else
  return {"c", "go", "rust"};
#endif
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndLanguages, DynamicModulesStatsSinkIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(testModuleLanguages())),
    DynamicModulesStatsSinkIntegrationTest::testParamsToString);

TEST_P(DynamicModulesStatsSinkIntegrationTest, BasicFlush) {
  // The "found gauge server.uptime" marker proves the module decoded a gauge name through the
  // buffer-based snapshot API.
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"info", "stat sink integration test: config_new called"},
                                 {"info", "stat sink integration test: flush called"},
                                 {"info", "stat sink integration test: found gauge server.uptime"},
                             }),
                             {
                               addStatSinkAndInitialize();
                               timeSystem().realSleepDoNotUseWithoutScrutiny(
                                   std::chrono::milliseconds(500));
                             });
}

TEST_P(DynamicModulesStatsSinkIntegrationTest, FlushAfterTraffic) {
  auto body = [this]() {
    addStatSinkAndInitialize();
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
    timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(500));
  };
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"info", "stat sink integration test: flush called"},
                                 {"info", "stat sink integration test: histogram complete"},
                             }),
                             body());
}

// The Rust and Go modules aggregate each flush snapshot on a worker thread and commit the result
// back to the main thread, where the scheduled hook publishes it into a gauge. Waiting for the
// gauge to reach a non-zero value proves the full off-main-thread round trip works end to end. The
// snapshot is copied on the main thread, aggregated on the worker, committed, then published on the
// main thread via the scheduler and gauge callbacks.
TEST_P(DynamicModulesStatsSinkIntegrationTest, OffThreadAggregationPublishesGauge) {
  if (language() == "c") {
    GTEST_SKIP() << "off-thread aggregation is only implemented in the Rust and Go test modules";
  }
  addStatSinkAndInitialize();
  // The gauge name has no prefix, so the published value is the sum of the flush snapshot counter
  // values, which becomes non-zero as Envoy increments counters.
  test_server_->waitForGauge("integration_aggregated_counters", testing::Ge(1));
}

} // namespace
} // namespace Envoy
