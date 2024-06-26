#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"

using testing::AssertionResult;

constexpr char EXPECTED_REQUEST_MESSAGE[] = R"EOF(
    resource_logs:
      resource:
        attributes:
          - key: "log_name"
            value:
              string_value: "foo"
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

class AccessLogIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat 'grpc.accesslog.streams_closed_1' and stat_prefix
    // 'accesslog'.
    skip_tag_extraction_rule_check_ = true;
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
    });

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("grpc_accesslog");

          envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig
              config;
          auto* common_config = config.mutable_common_config();
          common_config->set_log_name("foo");
          common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
          setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                         fake_upstreams_.back()->localAddress());
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
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest request_msg;
    VERIFY_ASSERTION(access_log_request_->waitForGrpcMessage(*dispatcher_, request_msg));
    EXPECT_EQ("POST", access_log_request_->headers().getMethodValue());
    EXPECT_EQ("/opentelemetry.proto.collector.logs.v1.LogsService/Export",
              access_log_request_->headers().getPathValue());
    EXPECT_EQ("application/grpc", access_log_request_->headers().getContentTypeValue());

    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest expected_request_msg;
    TestUtility::loadFromYaml(expected_request_msg_yaml, expected_request_msg);
    // Clear start time which is not deterministic.
    request_msg.mutable_resource_logs(0)
        ->mutable_scope_logs(0)
        ->mutable_log_records(0)
        ->clear_time_unix_nano();

    EXPECT_TRUE(TestUtility::protoEqual(request_msg, expected_request_msg,
                                        /*ignore_repeated_field_ordering=*/false));
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse response;
    access_log_request_->startGrpcStream();
    access_log_request_->sendGrpcMessage(response);
    access_log_request_->finishGrpcStream(Grpc::Status::Ok);
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

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, AccessLogIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test a basic full access logging flow.
TEST_P(AccessLogIntegrationTest, BasicAccessLogFlow) {
  testRouterNotFound();
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(EXPECTED_REQUEST_MESSAGE));

  // Make another request and expect a new stream to be used.
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(EXPECTED_REQUEST_MESSAGE));
  cleanup();
}

TEST_P(AccessLogIntegrationTest, AccessLoggerStatsAreIndependentOfListener) {
  const std::string expected_access_log_results = R"EOF(
    resource_logs:
      resource:
        attributes:
          - key: "log_name"
            value:
              string_value: "foo"
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
              string_value: "GET HTTP/1.1 200"
            attributes:
              - key: "response_code_details"
                value:
                  string_value: "via_upstream"
  )EOF";
  autonomous_upstream_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(expected_access_log_results));

  // LDS update to modify the listener and corresponding drain.
  // The config has the same GRPC access logger so it is not removed from the
  // cache.
  {
    ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
    new_config_helper.addConfigModifier(
        [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->mutable_listener_filters_timeout()->set_seconds(10);
        });
    new_config_helper.setLds("1");
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  }

  // Make another request, the existing grpc access logger should be used.
  auto codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());

  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(expected_access_log_results));
  codec_client_->close();
  cleanup();

  test_server_->waitForCounterEq("access_logs.open_telemetry_access_log.logs_written", 2);
}

} // namespace
} // namespace Envoy
