#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
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

  std::string moduleName() const {
    if (language() == "go") {
      return "stat_sink";
    }
    return "stat_sink_integration_test";
  }

  void setUpTestModulePath() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath(moduleName(), language());
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void addStatSinkAndInitialize() {
    setUpTestModulePath();
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

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndLanguages, DynamicModulesStatsSinkIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values("c", "go")),
    DynamicModulesStatsSinkIntegrationTest::testParamsToString);

TEST_P(DynamicModulesStatsSinkIntegrationTest, BasicFlush) {
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"info", "stat sink integration test: config_new called"},
                                 {"info", "stat sink integration test: flush called"},
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
  EXPECT_LOG_CONTAINS("info", "stat sink integration test: flush called", body());
}

} // namespace
} // namespace Envoy
