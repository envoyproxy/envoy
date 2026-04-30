#include "envoy/extensions/tracers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

// Parameterized over (language, ip_version). Each language ships a tracer_integration_test
// module exposing a "test_tracer" tracer that returns recording spans. The test asserts
// that requests flow through HCM with the dynamic-module tracer attached and don't
// crash — i.e., span creation, tag/log dispatch, and span finish all work via cgo.
struct TracerIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModuleTracerIntegrationTest : public testing::TestWithParam<TracerIntegrationParam>,
                                            public HttpIntegrationTest {
public:
  DynamicModuleTracerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  void initializeWithTracer() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* tracing = hcm.mutable_tracing();
          // Force sampling so spans are actually created.
          tracing->mutable_random_sampling()->set_value(100.0);

          auto* provider = tracing->mutable_provider();
          provider->set_name("envoy.tracers.dynamic_modules");

          envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer tracer_proto;
          tracer_proto.mutable_dynamic_module_config()->set_name("tracer_integration_test");
          tracer_proto.set_tracer_name("test_tracer");
          Protobuf::StringValue value;
          value.set_value("test_config");
          tracer_proto.mutable_tracer_config()->PackFrom(value);
          provider->mutable_typed_config()->PackFrom(tracer_proto);
        });
    initialize();
  }
};

namespace {
std::vector<TracerIntegrationParam> getTracerTestParams() {
  std::vector<TracerIntegrationParam> params;
  for (const auto& language : {"rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string tracerParamName(const testing::TestParamInfo<TracerIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModuleTracerIntegrationTest,
                         testing::ValuesIn(getTracerTestParams()), tracerParamName);

TEST_P(DynamicModuleTracerIntegrationTest, BasicTracingFlow) {
  initializeWithTracer();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModuleTracerIntegrationTest, MultipleRequests) {
  initializeWithTracer();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  for (int i = 0; i < 3; ++i) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }
}

} // namespace
} // namespace Envoy
