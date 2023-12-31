#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/retry_admission_control/config.pb.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

class RetryAdmissionControlIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(Protocols, RetryAdmissionControlIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(RetryAdmissionControlIntegrationTest, BlockAllRetries) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    auto* thresholds = circuit_breakers->add_thresholds();

    thresholds->mutable_retry_admission_control()->set_name("envoy.retry_admission_control.custom");
    test::integration::retry_admission_control::CustomRetryAdmissionControlConfig config;
    config.set_allow_retries(false);
    thresholds->mutable_retry_admission_control()->mutable_typed_config()->PackFrom(config);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_nanos(
            1000000); // min backoff
      });

  initialize();

  // non-retried request works
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());

  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());
}

TEST_P(RetryAdmissionControlIntegrationTest, AllowAllRetries) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    auto* thresholds = circuit_breakers->add_thresholds();

    thresholds->mutable_retry_admission_control()->set_name("envoy.retry_admission_control.custom");
    test::integration::retry_admission_control::CustomRetryAdmissionControlConfig config;
    config.set_allow_retries(true);
    thresholds->mutable_retry_admission_control()->mutable_typed_config()->PackFrom(config);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(100);
        retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_nanos(
            1000000); // min backoff
      });

  initialize();

  // non-retried request works
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());

  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  for (uint64_t i = 0; i < 10; i++) {
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    ASSERT_TRUE(upstream_request_->complete());
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());
}

TEST_P(RetryAdmissionControlIntegrationTest, DiableViaRuntimeFeatureGuard) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    auto* thresholds = circuit_breakers->add_thresholds();

    thresholds->mutable_retry_admission_control()->set_name("envoy.retry_admission_control.custom");
    test::integration::retry_admission_control::CustomRetryAdmissionControlConfig config;
    config.set_allow_retries(false);
    thresholds->mutable_retry_admission_control()->mutable_typed_config()->PackFrom(config);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_nanos(
            1000000); // min backoff
      });

  initialize();

  // disable retry admission control via runtime feature guard
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_retry_admission_control", "false"}});

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());
}

} // namespace
} // namespace Envoy
