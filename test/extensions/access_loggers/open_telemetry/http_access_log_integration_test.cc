#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/version/version.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"

using testing::AssertionResult;

constexpr char EXPECTED_REQUEST_MESSAGE[] = R"EOF(
    resource_logs:
      resource:
        attributes:
          - key: "log_name"
            value:
              string_value: "otel_envoy_accesslog"
          - key: "zone_name"
            value:
              string_value: "zone_name"
          - key: "cluster_name"
            value:
              string_value: "cluster_name"
          - key: "node_name"
            value:
              string_value: "node_name"
      scope_logs:
        - log_records:
            body:
              string_value: "GET HTTP/1.1 404"
            attributes:
              - key: "response_code_details"
                value:
                  string_value: "route_not_found"
  )EOF";

namespace Envoy {
namespace {

class HttpAccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  HttpAccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Add HTTP upstream for access log receiver.
    addFakeUpstream(Http::CodecType::HTTP1);
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
    });

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.access_loggers.open_telemetry");

          envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig
              config;
          config.set_log_name("otel_envoy_accesslog");
          auto* http_service = config.mutable_http_service();
          http_service->mutable_http_uri()->set_uri(fmt::format(
              "http://{}:{}/v1/logs", Network::Test::getLoopbackAddressUrlString(GetParam()),
              fake_upstreams_.back()->localAddress()->ip()->port()));
          http_service->mutable_http_uri()->set_cluster("accesslog");
          http_service->mutable_http_uri()->mutable_timeout()->set_seconds(1);

          // Add custom Authorization header.
          auto* header = http_service->add_request_headers_to_add();
          header->mutable_header()->set_key("Authorization");
          header->mutable_header()->set_value("Bearer test-token");

          auto* body_config = config.mutable_body();
          body_config->set_string_value("%REQ(:METHOD)% %PROTOCOL% %RESPONSE_CODE%");
          auto* attr_config = config.mutable_attributes();
          auto* value = attr_config->add_values();
          value->set_key("response_code_details");
          value->mutable_value()->set_string_value("%RESPONSE_CODE_DETAILS%");
          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_access_log_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogStream() {
    return fake_access_log_connection_->waitForNewStream(*dispatcher_, access_log_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogRequest(const std::string& expected_request_msg_yaml) {
    VERIFY_ASSERTION(access_log_request_->waitForEndStream(*dispatcher_));

    // Verify HTTP method and path per OTLP HTTP spec.
    EXPECT_EQ("POST", access_log_request_->headers().getMethodValue());
    EXPECT_EQ("/v1/logs", access_log_request_->headers().getPathValue());
    EXPECT_EQ("application/x-protobuf", access_log_request_->headers().getContentTypeValue());

    // Verify Authorization header is present.
    auto auth_header = access_log_request_->headers().get(Http::LowerCaseString("authorization"));
    EXPECT_FALSE(auth_header.empty());
    EXPECT_EQ("Bearer test-token", auth_header[0]->value().getStringView());

    // Verify User-Agent follows OTLP spec.
    auto user_agent = access_log_request_->headers().getUserAgentValue();
    EXPECT_TRUE(absl::StartsWith(user_agent, "OTel-OTLP-Exporter-Envoy/"));

    // Parse and verify the protobuf body.
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest request_msg;
    EXPECT_TRUE(request_msg.ParseFromString(access_log_request_->body().toString()));

    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);
    // Clear start time which is not deterministic.
    request_msg.mutable_resource_logs(0)
        ->mutable_scope_logs(0)
        ->mutable_log_records(0)
        ->clear_time_unix_nano();

    EXPECT_TRUE(TestUtility::protoEqual(request_msg, expected_request_msg,
                                        /*ignore_repeated_field_ordering=*/false));

    // Send success response.
    access_log_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    return AssertionSuccess();
  }

  void cleanup() {
    if (fake_access_log_connection_ != nullptr) {
      AssertionResult result = fake_access_log_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_access_log_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_access_log_connection_;
  FakeStreamPtr access_log_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies end-to-end OTLP HTTP access logging with custom headers and body formatting.
TEST_P(HttpAccessLogIntegrationTest, BasicHttpAccessLogFlow) {
  testRouterNotFound();
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(EXPECTED_REQUEST_MESSAGE));
  cleanup();
}

} // namespace
} // namespace Envoy
