#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/base_overload_integration_test.h"
#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {
namespace {

class PriorityLoadShedIntegrationTest : public BaseOverloadIntegrationTest,
                                        public HttpProtocolIntegrationTest {
protected:
  void initializeOverloadManager(const envoy::config::overload::v3::OverloadManager& config) {
    overload_manager_config_ = config;
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
    updateResource(0);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, PriorityLoadShedIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2,
                              Http::CodecClient::Type::HTTP3},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(PriorityLoadShedIntegrationTest, UsesBucketLoadShedPoints) {
  autonomous_upstream_ = true;
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.priority_load_shed
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.priority_load_shed.v3.PriorityLoadShed
  header_name: x-message-priority
  buckets:
  - value_range: { start: 0, end: 16 }
    load_shed_point: envoy.load_shed_points.priority.high
  - value_range: { start: 16, end: 32 }
    load_shed_point: envoy.load_shed_points.priority.low
)EOF");

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(R"EOF(
      refresh_interval:
        seconds: 0
        nanos: 1000000
      resource_monitors:
      - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
        typed_config:
          "@type": type.googleapis.com/test.common.config.DummyConfig
      loadshed_points:
      - name: "envoy.load_shed_points.priority.high"
        triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.70
      - name: "envoy.load_shed_points.priority.low"
        triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  auto send_request_with_priority = [this](int priority) {
    auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "priority.example"}};
    request_headers.addCopy(Http::LowerCaseString("x-message-priority"), absl::StrCat(priority));
    auto response = codec_client->makeHeaderOnlyRequest(request_headers);
    EXPECT_TRUE(response->waitForEndStream());
    const std::string status(response->headers().getStatusValue());
    codec_client->close();
    return status;
  };

  updateResource(0.65);
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.high.scale_percent", Eq(0));
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.low.scale_percent", Eq(0));
  EXPECT_EQ("200", send_request_with_priority(5));
  EXPECT_EQ("200", send_request_with_priority(20));

  updateResource(0.75);
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.high.scale_percent",
                             Eq(100));
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.low.scale_percent", Eq(0));
  EXPECT_EQ("503", send_request_with_priority(5));
  EXPECT_EQ("200", send_request_with_priority(20));

  updateResource(0.95);
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.high.scale_percent",
                             Eq(100));
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.low.scale_percent", Eq(100));
  EXPECT_EQ("503", send_request_with_priority(5));
  EXPECT_EQ("503", send_request_with_priority(20));
}

TEST_P(PriorityLoadShedIntegrationTest, RejectsMissingHeader) {
  autonomous_upstream_ = true;
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.priority_load_shed
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.priority_load_shed.v3.PriorityLoadShed
  header_name: x-message-priority
  buckets:
  - value_range: { start: 0, end: 32 }
    load_shed_point: envoy.load_shed_points.priority.high
)EOF");

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(R"EOF(
      refresh_interval:
        seconds: 0
        nanos: 1000000
      resource_monitors:
      - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
        typed_config:
          "@type": type.googleapis.com/test.common.config.DummyConfig
      loadshed_points:
      - name: "envoy.load_shed_points.priority.high"
        triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.70
    )EOF"));

  updateResource(0.95);
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.high.scale_percent",
                             Eq(100));

  auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "priority.example"}};
  auto response = codec_client->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("missing priority header", response->body());
  codec_client->close();
}

TEST_P(PriorityLoadShedIntegrationTest, RejectsInvalidHeader) {
  autonomous_upstream_ = true;
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.priority_load_shed
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.priority_load_shed.v3.PriorityLoadShed
  header_name: x-message-priority
  buckets:
  - value_range: { start: 0, end: 32 }
    load_shed_point: envoy.load_shed_points.priority.high
)EOF");

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(R"EOF(
      refresh_interval:
        seconds: 0
        nanos: 1000000
      resource_monitors:
      - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
        typed_config:
          "@type": type.googleapis.com/test.common.config.DummyConfig
      loadshed_points:
      - name: "envoy.load_shed_points.priority.high"
        triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.70
    )EOF"));

  updateResource(0.95);
  test_server_->waitForGauge("overload.envoy.load_shed_points.priority.high.scale_percent",
                             Eq(100));

  auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "priority.example"}};
  request_headers.addCopy(Http::LowerCaseString("x-message-priority"), "not-a-number");
  auto response = codec_client->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("invalid priority header", response->body());
  codec_client->close();
}

} // namespace
} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
