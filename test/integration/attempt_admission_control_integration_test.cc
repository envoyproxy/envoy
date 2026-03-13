#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/attempt_admission_control/config.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

class AttemptAdmissionControlIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      auto* circuit_breakers = cluster->mutable_circuit_breakers();
      auto* thresholds = circuit_breakers->add_thresholds();

      thresholds->mutable_attempt_admission_control()->set_name(
          "envoy.attempt_admission_control.test");
      thresholds->mutable_attempt_admission_control()->mutable_typed_config()->PackFrom(
          attempt_config_);
    });

    HttpProtocolIntegrationTest::initialize();
  }

protected:
  test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig attempt_config_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, AttemptAdmissionControlIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(AttemptAdmissionControlIntegrationTest, BlockAllRetries) {
  attempt_config_.set_retries_to_allow(0);
  attempt_config_.set_allow_initial_request(true);
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

  // retried request fails
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());
}

TEST_P(AttemptAdmissionControlIntegrationTest, AllowAllRetries) {
  attempt_config_.set_retries_to_allow(5);
  attempt_config_.set_allow_initial_request(true);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(5);
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

  // failed requests are retried.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  for (uint32_t i = 0; i < 4; ++i) {
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

TEST_P(AttemptAdmissionControlIntegrationTest, InitialAttemptBlocked) {
  attempt_config_.set_allow_initial_request(false);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(5);
        retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_nanos(
            1000000); // min backoff
      });

  initialize();

  // The initial attempt isn't admitted and no retries occur.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("503", response1->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value());
}

} // namespace
} // namespace Envoy
