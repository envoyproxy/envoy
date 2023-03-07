#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace {

class HealthCheckIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.prependFilter(ConfigHelper::defaultHealthCheckFilter());
    HttpProtocolIntegrationTest::initialize();
  }
  absl::string_view request(const std::string port_key, const std::string method,
                            const std::string endpoint, BufferingStreamDecoderPtr& response) {
    response = IntegrationUtil::makeSingleRequest(lookupPort(port_key), method, endpoint, "",
                                                  downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    return response->headers().getStatusValue();
  }
};

// Add a health check filter and verify correct behavior when draining.
TEST_P(HealthCheckIntegrationTest, DrainCloseGradual) {
  // The probability of drain close increases over time. With a high timeout,
  // the probability will be very low, but the rapid retries prevent this from
  // increasing total test time.
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence([] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  while (!test_server_->counter("http.config_test.downstream_cx_drain_close")->value()) {
    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(HealthCheckIntegrationTest, DrainCloseImmediate) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence([] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

// Add a health check filter and verify correct computation of health based on upstream status.
TEST_P(HealthCheckIntegrationTest, ComputedHealthCheck) {
  config_helper_.prependFilter(R"EOF(
name: health_check
typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    pass_through_mode: false
    cluster_min_healthy_percentages:
        example_cluster_name: { value: 75 }
)EOF");
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Add a health check filter and verify correct computation of health based on upstream status.
TEST_P(HealthCheckIntegrationTest, ModifyBuffer) {
  config_helper_.prependFilter(R"EOF(
name: health_check
typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    pass_through_mode: false
    cluster_min_healthy_percentages:
        example_cluster_name: { value: 75 }
)EOF");
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(HealthCheckIntegrationTest, HealthCheck) {
  DISABLE_IF_ADMIN_DISABLED;
  initialize();

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("http", "POST", "/healthcheck", response));

  EXPECT_EQ("200", request("admin", "POST", "/healthcheck/fail", response));
  EXPECT_EQ("503", request("http", "GET", "/healthcheck", response));

  EXPECT_EQ("200", request("admin", "POST", "/healthcheck/ok", response));
  EXPECT_EQ("200", request("http", "GET", "/healthcheck", response));
}

TEST_P(HealthCheckIntegrationTest, HealthCheckWithoutServerStats) {
  DISABLE_IF_ADMIN_DISABLED;

  envoy::config::metrics::v3::StatsMatcher stats_matcher;
  stats_matcher.mutable_exclusion_list()->add_patterns()->set_prefix("server.");
  config_helper_.addConfigModifier(
      [stats_matcher](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        *bootstrap.mutable_stats_config()->mutable_stats_matcher() = stats_matcher;
      });
  initialize();

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("http", "POST", "/healthcheck", response));
  EXPECT_EQ("200", request("admin", "GET", "/stats", response));
  EXPECT_THAT(response->body(), Not(HasSubstr("server.")));

  EXPECT_EQ("200", request("admin", "POST", "/healthcheck/fail", response));
  EXPECT_EQ("503", request("http", "GET", "/healthcheck", response));
  EXPECT_EQ("200", request("admin", "GET", "/stats", response));
  EXPECT_THAT(response->body(), Not(HasSubstr("server.")));

  EXPECT_EQ("200", request("admin", "POST", "/healthcheck/ok", response));
  EXPECT_EQ("200", request("http", "GET", "/healthcheck", response));
  EXPECT_EQ("200", request("admin", "GET", "/stats", response));
  EXPECT_THAT(response->body(), Not(HasSubstr("server.")));
}

TEST_P(HealthCheckIntegrationTest, HealthCheckWithBufferFilter) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter());
  initialize();

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("http", "GET", "/healthcheck", response));
}

INSTANTIATE_TEST_SUITE_P(Protocols, HealthCheckIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
