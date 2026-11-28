#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

class DynamicModulesLbIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  DynamicModulesLbIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  void initializeWithLbModule(const std::string& module_name) {
    config_helper_.addConfigModifier(
        [module_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
          auto* policy = cluster_0->mutable_load_balancing_policy();

          envoy::extensions::load_balancing_policies::dynamic_modules::v3::
              DynamicModulesLoadBalancerConfig lb_proto;
          lb_proto.mutable_dynamic_module_config()->set_name(module_name);
          lb_proto.set_lb_policy_name("test_lb");

          auto* entry = policy->add_policies()->mutable_typed_extension_config();
          entry->set_name("envoy.load_balancing_policies.dynamic_modules");
          std::ignore = entry->mutable_typed_config()->PackFrom(lb_proto);
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesLbIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that on_server_initialized and on_timer fire during real server startup.
TEST_P(DynamicModulesLbIntegrationTest, StartupLifecycleAndTimer) {
  initializeWithLbModule("lb_integration_test");
  test_server_->waitForCounter("dynamicmodulescustom.server_initialized", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.timer_fired", testing::Ge(1));
  test_server_->waitForCounter("dynamicmodulescustom.startup_callout_cluster_not_found",
                               testing::Ge(1));
}

// Verifies that an asynchronous HTTP callout sent by the LB module completes through
// Envoy's real async client and delivers the response back to on_http_callout_done.
TEST_P(DynamicModulesLbIntegrationTest, MainThreadHttpCalloutCompletes) {
  initializeWithLbModule("lb_integration_test");
  test_server_->waitForCounter("dynamicmodulescustom.callout_success", testing::Ge(1));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/test", "", Http::CodecType::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
