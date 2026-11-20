#include <chrono>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {
namespace {

using ::testing::Ge;
using ::testing::HasSubstr;

class PreviousPrioritiesIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  PreviousPrioritiesIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest()[0]) {}
};

// Regression test: an LDS update with an invalid PreviousPrioritiesConfig (update_frequency: 0)
// must be rejected at config load time rather than silently accepted and crashing the worker
// thread via an uncaught EnvoyException on the first matching request.
TEST_F(PreviousPrioritiesIntegrationTest, InvalidConfigRejectedAtLoad) {
  autonomous_upstream_ = true;
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        auto* retry_priority = retry_policy->mutable_retry_priority();
        retry_priority->set_name("envoy.retry_priorities.previous_priorities");
        envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig
            priority_config;
        priority_config.set_update_frequency(0);
        ASSERT_TRUE(retry_priority->mutable_typed_config()->PackFrom(priority_config));
      });
  new_config_helper.setLds("1");

  EXPECT_TRUE(TestUtility::waitForCounter(test_server_->statStore(),
                                          "listener_manager.lds.update_rejected", Ge(1),
                                          timeSystem(), std::chrono::milliseconds(5000)));
  EXPECT_EQ(0, test_server_->counter("listener_manager.listener_in_place_updated")->value());

  // The previously loaded listener must keep serving traffic cleanly. If the invalid retry
  // priority config had been accepted above, sending a request here throws on the worker thread.
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\nHost: host\r\nConnection: close\r\n\r\n",
                                &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));
}

// Verifies that disabling the runtime guard restores the legacy behavior where an invalid
// PreviousPrioritiesConfig is accepted at LDS load time.
TEST_F(PreviousPrioritiesIntegrationTest, InvalidConfigAcceptedAtLoadWhenRuntimeGuardDisabled) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.validate_retry_priority_config_at_load", "false");
  autonomous_upstream_ = true;
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        auto* retry_priority = retry_policy->mutable_retry_priority();
        retry_priority->set_name("envoy.retry_priorities.previous_priorities");
        envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig
            priority_config;
        priority_config.set_update_frequency(0);
        ASSERT_TRUE(retry_priority->mutable_typed_config()->PackFrom(priority_config));
      });
  new_config_helper.setLds("1");

  EXPECT_TRUE(TestUtility::waitForCounter(test_server_->statStore(),
                                          "listener_manager.lds.update_success", Ge(2),
                                          timeSystem(), std::chrono::milliseconds(5000)));
  EXPECT_EQ(0, test_server_->counter("listener_manager.lds.update_rejected")->value());
}

// Verify that a valid PreviousPrioritiesConfig (update_frequency: 1) routes retries to the next
// priority level after P0 returns 503.
TEST_F(PreviousPrioritiesIntegrationTest, ValidConfigRetriesAcrossPriorities) {
  use_lds_ = false;
  setUpstreamCount(2);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        auto* retry_priority = retry_policy->mutable_retry_priority();
        retry_priority->set_name("envoy.retry_priorities.previous_priorities");
        envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig
            priority_config;
        priority_config.set_update_frequency(1);
        ASSERT_TRUE(retry_priority->mutable_typed_config()->PackFrom(priority_config));
      });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();
    for (int i = 0; i < 2; ++i) {
      auto* locality = load_assignment->add_endpoints();
      locality->set_priority(i);
      locality->mutable_locality()->set_region("region");
      locality->mutable_locality()->set_zone("zone");
      locality->mutable_locality()->set_sub_zone("sub_zone" + std::to_string(i));
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  // First attempt goes to P0; respond with 503 to trigger retry on P1.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
