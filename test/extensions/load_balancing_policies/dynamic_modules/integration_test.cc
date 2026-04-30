#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

// Parameterized over (language, ip_version). Each language ships a
// load_balancer_integration_test module with a "first_host_lb" LB that selects priority
// 0, index 0. The default test cluster has exactly one host so the trivial selection
// always succeeds; the test asserts requests route through Envoy with the
// dynamic-module LB attached, exercising the per-worker LB Create + ChooseHost path.
struct LoadBalancerIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModuleLoadBalancerIntegrationTest
    : public testing::TestWithParam<LoadBalancerIntegrationParam>,
      public HttpIntegrationTest {
public:
  DynamicModuleLoadBalancerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  void initializeWithLoadBalancer() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG);

      auto* policy = cluster->mutable_load_balancing_policy()->add_policies();
      policy->mutable_typed_extension_config()->set_name(
          "envoy.load_balancing_policies.dynamic_modules");

      envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
          lb_config;
      lb_config.mutable_dynamic_module_config()->set_name("load_balancer_integration_test");
      lb_config.set_lb_policy_name("first_host_lb");
      policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(lb_config);
    });
    initialize();
  }
};

namespace {
std::vector<LoadBalancerIntegrationParam> getLoadBalancerTestParams() {
  std::vector<LoadBalancerIntegrationParam> params;
  for (const auto& language : {"rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string lbParamName(const testing::TestParamInfo<LoadBalancerIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModuleLoadBalancerIntegrationTest,
                         testing::ValuesIn(getLoadBalancerTestParams()), lbParamName);

TEST_P(DynamicModuleLoadBalancerIntegrationTest, RoutesViaDynamicModuleLB) {
  initializeWithLoadBalancer();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
